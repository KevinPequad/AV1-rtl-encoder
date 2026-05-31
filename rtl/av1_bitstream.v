// av1_bitstream.v — AV1 OBU Bitstream Generator
// Generates OBU (Open Bitstream Unit) headers and fixed bitstream structures:
//   - Temporal Delimiter OBU
//   - Sequence Header OBU
//   - Frame Header (within Frame OBU)
//
// Reference: AV1 Specification Section 5 (OBU syntax)
//            SVT-AV1/Source/Lib/Codec/bitstream_unit.c
//
// Ownership note: this module intentionally implements the current reduced
// static-CDF subset only. It owns the bytes for the supported temporal
// delimiter / sequence header / frame header fields, but broader AV1 header
// features (adaptive CDF update signaling, order hints, multi-ref refresh,
// non-trivial tile/filter/restoration tools, lossless/qindex=0 variants, etc.)
// must be added here with matching RTL-owned gates before they are claimed.

module av1_bitstream #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720
) (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        write_td,         // Write temporal delimiter OBU
    input  wire        write_seq_hdr,    // Write sequence header OBU
    input  wire        write_frame_hdr,  // Write frame header
    input  wire        is_keyframe,
    input  wire [7:0]  qindex,
    input  wire [3:0]  frame_num,
    input  wire        dc_only,
    input  wire [7:0]  refresh_frame_flags_in,
    input  wire [20:0] ref_frame_idx_map_in,

    output reg         busy,
    output reg         done,

    // Output byte stream
    output reg         byte_valid,
    output reg  [7:0]  byte_out,
    output reg  [23:0] bytes_written
);

    reg [7:0]  obuf [0:63];   // Output buffer
    reg [5:0]  obuf_len;
    reg [5:0]  obuf_idx;
    reg [2:0]  state;
    localparam integer FRAME_OBU_SIZE_BYTES = 4;

    // P9 disabled-filter ownership policy: the current low-delay subset does
    // not implement post-reconstruction loop filtering, CDEF, or restoration.
    // Keep the syntax fields explicit here so header tests fail before any
    // filtered-reference dependency can be introduced accidentally.
    localparam integer P9_ENABLE_CDEF          = 0;
    localparam integer P9_ENABLE_RESTORATION   = 0;
    localparam integer P9_LOOP_FILTER_LEVEL_0  = 0;
    localparam integer P9_LOOP_FILTER_LEVEL_1  = 0;
    localparam integer P9_LOOP_FILTER_SHARPNESS = 0;
    localparam integer P9_LOOP_FILTER_DELTA_EN = 0;

    localparam S_IDLE  = 3'd0;
    localparam S_BUILD = 3'd1;
    localparam S_OUT   = 3'd2;

    // Latch which build operation was requested
    reg [1:0]  build_cmd;
    localparam CMD_SEQ = 2'd1;
    localparam CMD_FRM = 2'd2;

    // Latch frame header inputs
    reg        lat_is_keyframe;
    reg [7:0]  lat_qindex;
    reg        lat_dc_only;
    reg [7:0]  lat_refresh_frame_flags;
    reg [20:0] lat_ref_frame_idx_map;
    reg        seq_hdr_emitted;
    integer    bw_byte_idx;
    integer    bw_bit_pos;
    integer    w_bits;
    integer    h_bits;
    integer    mi_cols_aligned;
    integer    mi_rows_aligned;
    integer    sb_cols;
    integer    sb_rows;
    integer    max_log2_tile_cols;
    integer    max_log2_tile_rows;
    integer    min_log2_tile_cols;
    integer    min_log2_tiles;
    integer    min_log2_tile_rows;
    integer    tile_cols_log2;
    integer    tile_rows_log2;
    integer    payload_len;
    integer    payload_len_tmp;
    integer    j;

    function integer bits_needed;
        input integer val;
        integer tmp;
        integer k;
        begin
            bits_needed = 0;
            tmp = val - 1;
            // Static bound for synthesis frontends; equivalent to shifting
            // tmp until it reaches zero for the supported integer width.
            for (k = 0; k < 32; k = k + 1) begin
                if (tmp > 0) begin
                    bits_needed = bits_needed + 1;
                    tmp = tmp >> 1;
                end
            end
            if (bits_needed < 1)
                bits_needed = 1;
        end
    endfunction

    function integer tile_log2;
        input integer blk_size;
        input integer target;
        integer k;
        integer blk_scaled;
        begin
            tile_log2 = 0;
            blk_scaled = blk_size;
            // Static bound for synthesis frontends. Only keep shifting while
            // the original dynamic loop would still be active, avoiding later
            // signed overflow from changing the result.
            for (k = 0; k < 32; k = k + 1) begin
                if (blk_scaled < target) begin
                    tile_log2 = tile_log2 + 1;
                    blk_scaled = blk_scaled << 1;
                end
            end
        end
    endfunction

    function [2:0] ref_map_slot;
        input [20:0] map;
        input integer idx;
        begin
            case (idx)
                0: ref_map_slot = map[2:0];
                1: ref_map_slot = map[5:3];
                2: ref_map_slot = map[8:6];
                3: ref_map_slot = map[11:9];
                4: ref_map_slot = map[14:12];
                5: ref_map_slot = map[17:15];
                6: ref_map_slot = map[20:18];
                default: ref_map_slot = 3'd0;
            endcase
        end
    endfunction

    task bw_reset;
        input integer start_idx;
        begin
            bw_byte_idx = start_idx;
            bw_bit_pos  = 0;
        end
    endtask

    task bw_write_bit;
        input integer b;
        begin
            if (bw_bit_pos == 0)
                obuf[bw_byte_idx] = 8'd0;
            obuf[bw_byte_idx] = obuf[bw_byte_idx] | ((b & 1) << (7 - bw_bit_pos));
            if (bw_bit_pos == 7) begin
                bw_bit_pos  = 0;
                bw_byte_idx = bw_byte_idx + 1;
            end else begin
                bw_bit_pos = bw_bit_pos + 1;
            end
        end
    endtask

    task bw_write_bits;
        input integer val;
        input integer nbits;
        integer k;
        begin
            // Keep the loop bounds static for synthesis frontends while
            // preserving MSB-first writes for the low nbits of val.
            for (k = 31; k >= 0; k = k - 1) begin
                if (k < nbits)
                    bw_write_bit((val >> k) & 1);
            end
        end
    endtask

    task bw_write_trailing_bits;
        integer k;
        begin
            bw_write_bit(1);
            // At most seven zero pad bits are needed to byte-align.
            for (k = 0; k < 7; k = k + 1) begin
                if (bw_bit_pos != 0)
                    bw_write_bit(0);
            end
        end
    endtask

    task bw_flush_zero_pad;
        begin
            if (bw_bit_pos != 0) begin
                bw_byte_idx = bw_byte_idx + 1;
                bw_bit_pos  = 0;
            end
        end
    endtask

    task bw_write_color_config;
        begin
            bw_write_bit(0);
            bw_write_bit(0);
            bw_write_bit(0);
            bw_write_bit(0);
            bw_write_bits(0, 2);
            bw_write_bit(0);
        end
    endtask

    task bw_write_tile_info;
        integer sb_cols_min;
        integer sb_rows_min;
        begin
            mi_cols_aligned = ((FRAME_WIDTH / 4) + 15) & ~15;
            mi_rows_aligned = ((FRAME_HEIGHT / 4) + 15) & ~15;
            sb_cols = mi_cols_aligned >> 4;
            sb_rows = mi_rows_aligned >> 4;

            min_log2_tile_cols = tile_log2(64, sb_cols);
            sb_cols_min = (sb_cols < 64) ? sb_cols : 64;
            sb_rows_min = (sb_rows < 64) ? sb_rows : 64;
            max_log2_tile_cols = tile_log2(1, sb_cols_min);
            max_log2_tile_rows = tile_log2(1, sb_rows_min);
            min_log2_tiles = tile_log2(576, sb_cols * sb_rows);
            if (min_log2_tile_cols > min_log2_tiles)
                min_log2_tiles = min_log2_tile_cols;

            bw_write_bit(1);  // uniform_tile_spacing_flag

            tile_cols_log2 = min_log2_tile_cols;
            if (tile_cols_log2 < max_log2_tile_cols)
                bw_write_bit(0);

            min_log2_tile_rows = min_log2_tiles - tile_cols_log2;
            if (min_log2_tile_rows < 0)
                min_log2_tile_rows = 0;
            tile_rows_log2 = min_log2_tile_rows;
            if (tile_rows_log2 < max_log2_tile_rows)
                bw_write_bit(0);
        end
    endtask

    task bw_write_quantization_params;
        input [7:0] qidx;
        begin
            bw_write_bits(qidx, 8);
            bw_write_bit(0);
            bw_write_bit(0);
            bw_write_bit(0);
            bw_write_bit(0);
            bw_write_bit(0);
        end
    endtask

    task bw_write_loop_filter_params;
        begin
            bw_write_bits(P9_LOOP_FILTER_LEVEL_0, 6);
            bw_write_bits(P9_LOOP_FILTER_LEVEL_1, 6);
            bw_write_bits(P9_LOOP_FILTER_SHARPNESS, 3);
            bw_write_bit(P9_LOOP_FILTER_DELTA_EN);
        end
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            busy          <= 0;
            done          <= 0;
            byte_valid    <= 0;
            bytes_written <= 0;
            state         <= S_IDLE;
            obuf_len      <= 0;
            obuf_idx      <= 0;
            build_cmd     <= 0;
            lat_is_keyframe <= 0;
            lat_qindex <= 0;
            lat_dc_only <= 1'b1;
            lat_refresh_frame_flags <= 8'h01;
            lat_ref_frame_idx_map <= 21'd0;
            seq_hdr_emitted <= 1'b0;
        end else begin
            done       <= 0;
            byte_valid <= 0;

            case (state)
                S_IDLE: begin
                    if (write_td) begin
                        // Temporal Delimiter OBU: type=2, has_size=1
                        // Header: 0b0_0010_0_1_0 = 0x12, size = 0
                        obuf[0]  <= 8'h12;
                        obuf[1]  <= 8'h00;
                        obuf_len <= 6'd2;
                        obuf_idx <= 0;
                        busy     <= 1;
                        state    <= S_OUT;
                    end else if (write_seq_hdr) begin
                        if (seq_hdr_emitted) begin
                            done <= 1;
                            busy <= 0;
                        end else begin
                            build_cmd <= CMD_SEQ;
                            busy      <= 1;
                            state     <= S_BUILD;
                        end
                    end else if (write_frame_hdr) begin
                        build_cmd       <= CMD_FRM;
                        lat_is_keyframe <= is_keyframe;
                        lat_qindex      <= qindex;
                        lat_dc_only     <= dc_only;
                        lat_refresh_frame_flags <= refresh_frame_flags_in;
                        lat_ref_frame_idx_map <= ref_frame_idx_map_in;
                        busy            <= 1;
                        state           <= S_BUILD;
                    end
                end

                S_BUILD: begin
                    obuf_idx <= 0;
                    for (j = 0; j < 64; j = j + 1)
                        obuf[j] = 8'd0;

                    if (build_cmd == CMD_SEQ) begin
                        // Match the software-owned debug path's video sequence
                        // header for the current 8-bit 4:2:0 single-layer
                        // subset. Even the all-key bring-up path needs the
                        // non-still sequence form to stay aligned with the
                        // multi-frame ownership target.
                        obuf[0] = 8'h0A;
                        bw_reset(2);
                        bw_write_bits(0, 3);
                        bw_write_bit(0);   // still_picture
                        bw_write_bit(0);   // reduced_still_picture_header
                        bw_write_bit(0);   // timing_info_present_flag
                        bw_write_bit(0);   // initial_display_delay_present_flag
                        bw_write_bits(0, 5);   // operating_points_cnt_minus_1
                        bw_write_bits(0, 12);  // operating_point_idc
                        bw_write_bits(4, 5);  // seq_level_idx
                        w_bits = bits_needed(FRAME_WIDTH);
                        h_bits = bits_needed(FRAME_HEIGHT);
                        bw_write_bits(w_bits - 1, 4);
                        bw_write_bits(h_bits - 1, 4);
                        bw_write_bits(FRAME_WIDTH - 1, w_bits);
                        bw_write_bits(FRAME_HEIGHT - 1, h_bits);
                        bw_write_bit(0);   // frame_id_numbers_present_flag
                        bw_write_bit(0);   // use_128x128_superblock
                        bw_write_bit(0);   // enable_filter_intra
                        bw_write_bit(0);   // enable_intra_edge_filter
                        bw_write_bit(0);   // enable_interintra_compound
                        bw_write_bit(0);   // enable_masked_compound
                        bw_write_bit(0);   // enable_warped_motion
                        bw_write_bit(0);   // enable_dual_filter
                        bw_write_bit(0);   // enable_order_hint
                        bw_write_bit(1);   // seq_choose_screen_content_tools
                        bw_write_bit(1);   // seq_choose_integer_mv
                        bw_write_bit(0);   // enable_superres
                        bw_write_bit(P9_ENABLE_CDEF);   // enable_cdef: disabled by P9 policy
                        bw_write_bit(P9_ENABLE_RESTORATION);   // enable_restoration: disabled by P9 policy
                        bw_write_color_config();
                        bw_write_bit(0);   // film_grain_params_present
                        bw_write_trailing_bits();
                        payload_len = bw_byte_idx - 2;
                        obuf[1] = payload_len[7:0];
                        obuf_len <= bw_byte_idx[5:0];
                        seq_hdr_emitted <= 1'b1;
                    end else begin
                        // Keep the non-key path as a reduced placeholder for
                        // now. For keyframes, emit the software writer's
                        // video keyframe header fields so the RTL raw bytes
                        // stay aligned with the non-still ownership target.
                        obuf[0] = 8'h32;
                        obuf[1] = 8'h80;
                        obuf[2] = 8'h80;
                        obuf[3] = 8'h80;
                        obuf[4] = 8'h00;
                        if (lat_is_keyframe) begin
                            bw_reset(1 + FRAME_OBU_SIZE_BYTES);
                            bw_write_bit(0);   // show_existing_frame
                            bw_write_bits(0, 2);   // frame_type = KEY_FRAME
                            bw_write_bit(1);   // show_frame
                            // The current raw RTL tile path still uses the
                            // default CDF tables without adaptive updates.
                            // Keep the owned raw bitstream aligned with that
                            // subset until live CDF adaptation moves onto the
                            // RTL path.
                            bw_write_bit(1);   // disable_cdf_update
                            bw_write_bit(0);   // allow_screen_content_tools
                            bw_write_bit(0);   // frame_size_override_flag
                            bw_write_bit(0);   // render_and_frame_size_different
                            bw_write_tile_info();
                            bw_write_quantization_params(lat_qindex);
                            bw_write_bit(0);   // segmentation_enabled
                            bw_write_bit(0);   // delta_q_present
                            bw_write_loop_filter_params();
                            bw_write_bit(0);   // tx_mode_select
                            bw_write_bit(0);   // reduced_tx_set
                            bw_flush_zero_pad();
                            payload_len = bw_byte_idx - (1 + FRAME_OBU_SIZE_BYTES);
                            payload_len_tmp = payload_len;
                            obuf[1] = {1'b1, payload_len_tmp[6:0]};
                            payload_len_tmp = payload_len_tmp >> 7;
                            obuf[2] = {1'b1, payload_len_tmp[6:0]};
                            payload_len_tmp = payload_len_tmp >> 7;
                            obuf[3] = {1'b1, payload_len_tmp[6:0]};
                            payload_len_tmp = payload_len_tmp >> 7;
                            obuf[4] = {1'b0, payload_len_tmp[6:0]};
                            obuf_len <= bw_byte_idx[5:0];
                        end else begin
                            // Reduced video inter-frame header matching the
                            // software reference writer's current single-ref,
                            // integer-MV subset.
                            bw_reset(1 + FRAME_OBU_SIZE_BYTES);
                            bw_write_bit(0);   // show_existing_frame
                            bw_write_bits(1, 2);   // frame_type = INTER_FRAME
                            bw_write_bit(1);   // show_frame
                            bw_write_bit(1);   // error_resilient_mode
                            bw_write_bit(1);   // disable_cdf_update
                            bw_write_bit(1);   // allow_screen_content_tools
                            bw_write_bit(!lat_dc_only);   // force_integer_mv for full-coeff integer-ME frames
                            bw_write_bit(0);   // frame_size_override_flag
                            bw_write_bits(lat_refresh_frame_flags, 8);   // refresh LAST slot only
                            for (j = 0; j < 7; j = j + 1)
                                bw_write_bits(ref_map_slot(lat_ref_frame_idx_map, j), 3);   // all refs map to LAST
                            bw_write_bit(0);   // render_and_frame_size_different
                            if (lat_dc_only)
                                bw_write_bit(1);   // allow_high_precision_mv
                            bw_write_bit(0);   // interpolation_filter == SWITCHABLE
                            bw_write_bits(0, 2);   // interpolation_filter = regular
                            bw_write_bit(0);   // is_motion_mode_switchable
                            // With disable_cdf_update=1, disable_frame_end_update_cdf
                            // is inferred and refresh_frame_context is not signaled.
                            bw_write_tile_info();
                            bw_write_quantization_params(lat_qindex);
                            bw_write_bit(0);   // segmentation_enabled
                            bw_write_bit(0);   // delta_q_present
                            bw_write_loop_filter_params();
                            bw_write_bit(0);   // tx_mode_select
                            bw_write_bit(0);   // reference_select = single reference
                            bw_write_bit(0);   // reduced_tx_set
                            for (j = 0; j < 7; j = j + 1)
                                bw_write_bit(0);   // global motion type = identity
                            bw_flush_zero_pad();
                            payload_len = bw_byte_idx - (1 + FRAME_OBU_SIZE_BYTES);
                            payload_len_tmp = payload_len;
                            obuf[1] = {1'b1, payload_len_tmp[6:0]};
                            payload_len_tmp = payload_len_tmp >> 7;
                            obuf[2] = {1'b1, payload_len_tmp[6:0]};
                            payload_len_tmp = payload_len_tmp >> 7;
                            obuf[3] = {1'b1, payload_len_tmp[6:0]};
                            payload_len_tmp = payload_len_tmp >> 7;
                            obuf[4] = {1'b0, payload_len_tmp[6:0]};
                            obuf_len <= bw_byte_idx[5:0];
                        end
                    end

                    state <= S_OUT;
                end

                S_OUT: begin
                    if (obuf_idx < obuf_len) begin
                        byte_valid    <= 1;
                        byte_out      <= obuf[obuf_idx];
                        obuf_idx      <= obuf_idx + 1;
                        bytes_written <= bytes_written + 1;
                    end else begin
                        busy  <= 0;
                        done  <= 1;
                        state <= S_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
