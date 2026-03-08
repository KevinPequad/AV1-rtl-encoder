// av1_encoder_top.v — Top-Level AV1 Encoder
// Main profile, 4:2:0, 8-bit, parameterized resolution (multiple of 16)
// I-frame + P-frame support, 64x64 superblocks with fixed 8x8 coding blocks
//
// Pipeline: fetch → predict → transform → quant → entropy →
//           inverse_quant → inverse_transform → reconstruct
// Then: bitstream output with OBU headers
//
// Reference: SVT-AV1 for all AV1-specific algorithms
//            H.264 RTL encoder for structural conventions

module av1_encoder_top #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720,
    parameter BLK_COLS     = FRAME_WIDTH  / 8,  // 8x8 block columns
    parameter BLK_ROWS     = FRAME_HEIGHT / 8,  // 8x8 block rows
    parameter SB_COLS      = (FRAME_WIDTH  + 63) / 64, // Superblock columns
    parameter SB_ROWS      = (FRAME_HEIGHT + 63) / 64  // Superblock rows
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    output reg         done,

    // Frame control
    input  wire [3:0]  frame_num_in,
    input  wire        is_keyframe_in,
    input  wire [7:0]  qindex_in,     // Quantization index (0-255)

    // Raw frame memory read port (YUV420 planar)
    output wire [20:0] raw_mem_addr,
    input  wire [7:0]  raw_mem_data,

    // Reference frame memory (luma only, read/write)
    output wire [19:0] ref_mem_rd_addr,
    input  wire [7:0]  ref_mem_rd_data,
    output wire        ref_rd_is_neigh,  // 1 = reading neighbors (use current frame)
    output reg         ref_mem_wr_en,
    output reg  [19:0] ref_mem_wr_addr,
    output reg  [7:0]  ref_mem_wr_data,

    // Chroma reference memory (Cb)
    output reg  [17:0] chr_cb_ref_rd_addr,
    input  wire [7:0]  chr_cb_ref_rd_data,
    output reg         chr_cb_ref_wr_en,
    output reg  [17:0] chr_cb_ref_wr_addr,
    output reg  [7:0]  chr_cb_ref_wr_data,

    // Chroma reference memory (Cr)
    output reg  [17:0] chr_cr_ref_rd_addr,
    input  wire [7:0]  chr_cr_ref_rd_data,
    output reg         chr_cr_ref_wr_en,
    output reg  [17:0] chr_cr_ref_wr_addr,
    output reg  [7:0]  chr_cr_ref_wr_data,

    // Bitstream memory write port
    output wire [23:0] bs_mem_addr,
    output wire [7:0]  bs_mem_data,
    output wire        bs_mem_wr,
    output wire [23:0] bs_bytes_written
);

    // ====================================================================
    // Frame geometry
    // ====================================================================
    localparam LUMA_SIZE   = FRAME_WIDTH * FRAME_HEIGHT;
    localparam CHROMA_W    = FRAME_WIDTH / 2;
    localparam CHROMA_H    = FRAME_HEIGHT / 2;
    localparam CHROMA_SIZE = CHROMA_W * CHROMA_H;

    // ====================================================================
    // Top-level FSM
    // ====================================================================
    localparam [5:0]
        TS_IDLE         = 6'd0,
        TS_WRITE_TD     = 6'd1,
        TS_WAIT_TD      = 6'd2,
        TS_WRITE_SEQ    = 6'd3,
        TS_WAIT_SEQ     = 6'd4,
        TS_WRITE_FRM    = 6'd5,
        TS_WAIT_FRM     = 6'd6,
        TS_FETCH_BLK    = 6'd7,
        TS_WAIT_FETCH   = 6'd8,
        TS_ME_START     = 6'd9,
        TS_WAIT_ME      = 6'd10,
        TS_PREDICT      = 6'd11,
        TS_WAIT_PRED    = 6'd12,
        TS_XFORM_ROW    = 6'd13,
        TS_XFORM_WAIT   = 6'd14,
        TS_XFORM_COL    = 6'd26,  // Forward column transform
        TS_XFORM_COL_WT = 6'd27,  // Wait for column transform done
        TS_QCOEFF_START = 6'd15,
        TS_QCOEFF_WAIT  = 6'd16,
        TS_IXFORM_COL   = 6'd28,  // Inverse column transform
        TS_IXFORM_COL_WT= 6'd29,  // Wait for inverse column transform
        TS_IXFORM       = 6'd17,
        TS_RECON        = 6'd18,
        TS_NEXT_BLK     = 6'd19,
        TS_REF_WR       = 6'd20,
        TS_CHR_FETCH    = 6'd21,  // Fetch chroma block
        TS_CHR_WAIT     = 6'd30,  // Wait for chroma fetch
        TS_CHR_WR       = 6'd31,  // Write chroma to ref memory
        TS_EC_WAIT      = 6'd23,
        TS_IQ_START     = 6'd24,
        TS_IQ_WAIT      = 6'd25,
        TS_DONE         = 6'd22,
        TS_NEIGH_ADDR   = 6'd32,  // Neighbor loading: issue address
        TS_NEIGH_READ   = 6'd33;  // Neighbor loading: read data

    reg [5:0]  top_state;
    reg [9:0]  blk_x, blk_y;    // Current block position (in 8x8 units)
    reg [3:0]  sub_idx;          // Sub-block index within processing
    reg        is_keyframe;
    reg [7:0]  qindex;
    reg [3:0]  frame_num;

    // Current block pixel buffer (8x8 = 64 pixels)
    reg [7:0]  cur_blk [0:63];

    // Prediction buffer
    reg [7:0]  pred_blk [0:63];

    // Residual buffer (after prediction subtraction)
    reg signed [15:0] residual [0:63];

    // Transform output buffer
    reg signed [15:0] xform_out [0:63];

    // Quantized coefficient buffer
    reg signed [15:0] qcoeff [0:63];

    // Dequant values
    reg [15:0] dequant_dc, dequant_ac;

    // Reconstructed block
    reg [7:0]  recon_blk [0:63];

    // Neighbor pixels for intra prediction
    reg [7:0]  top_pixels [0:7];
    reg [7:0]  left_pixels [0:7];
    reg [7:0]  top_left_pixel;
    reg        has_top, has_left;

    // Motion estimation results
    reg signed [8:0] me_mvx, me_mvy;
    reg [17:0] me_sad;
    reg        use_inter;  // 1 if P-frame and ME result is good

    // SAD threshold for choosing inter vs intra
    localparam [17:0] INTRA_SAD_THRESHOLD = 18'd4000;

    // Intra mode selection
    reg [2:0] best_intra_mode;

    // Processing counters
    reg [5:0]  proc_idx;
    reg [2:0]  xform_row;
    reg [2:0]  xform_col;
    reg [8:0]  ref_wr_idx;
    reg [4:0]  neigh_cnt;    // Neighbor loading counter (0=TL, 1-8=top, 9-16=left)

    // Chroma processing
    reg        chr_plane;     // 0=Cb, 1=Cr
    reg [4:0]  chr_wr_idx;    // 0..15 for 4x4 block
    reg [7:0]  chr_blk [0:15]; // 4x4 chroma block buffer

    // ====================================================================
    // Sub-module instantiation
    // ====================================================================

    // Fetch module
    wire        fetch_done;
    wire [20:0] fetch_mem_addr;
    wire [7:0]  fetch_pixel_buf [0:63];
    wire [5:0]  fetch_pixel_count;
    reg         fetch_start;
    reg         fetch_is_chroma, fetch_chroma_id;
    reg [9:0]   fetch_blk_x, fetch_blk_y;

    av1_fetch #(
        .FRAME_WIDTH(FRAME_WIDTH),
        .FRAME_HEIGHT(FRAME_HEIGHT)
    ) u_fetch (
        .clk(clk), .rst_n(rst_n),
        .start(fetch_start),
        .is_chroma(fetch_is_chroma),
        .chroma_id(fetch_chroma_id),
        .done(fetch_done),
        .blk_x(fetch_blk_x), .blk_y(fetch_blk_y),
        .mem_addr(fetch_mem_addr),
        .mem_data(raw_mem_data),
        .pixel_buf(fetch_pixel_buf),
        .pixel_count(fetch_pixel_count)
    );

    assign raw_mem_addr = fetch_mem_addr;

    // Transform module
    wire        xform_done;
    reg         xform_start;
    reg         xform_is_4x4;
    reg signed [15:0] xform_in [0:7];
    wire signed [15:0] xform_out_w [0:7];

    av1_transform u_transform (
        .clk(clk), .rst_n(rst_n),
        .start(xform_start),
        .is_4x4(xform_is_4x4),
        .done(xform_done),
        .in0(xform_in[0]), .in1(xform_in[1]),
        .in2(xform_in[2]), .in3(xform_in[3]),
        .in4(xform_in[4]), .in5(xform_in[5]),
        .in6(xform_in[6]), .in7(xform_in[7]),
        .out0(xform_out_w[0]), .out1(xform_out_w[1]),
        .out2(xform_out_w[2]), .out3(xform_out_w[3]),
        .out4(xform_out_w[4]), .out5(xform_out_w[5]),
        .out6(xform_out_w[6]), .out7(xform_out_w[7])
    );

    // Quantize module
    wire        quant_done;
    reg         quant_start;
    reg         quant_is_dc;
    reg signed [15:0] quant_coeff_in;
    wire signed [15:0] quant_coeff_out;
    wire [15:0] quant_dequant_out;

    av1_quantize u_quantize (
        .clk(clk), .rst_n(rst_n),
        .start(quant_start),
        .is_dc(quant_is_dc),
        .qindex(qindex),
        .done(quant_done),
        .coeff_in(quant_coeff_in),
        .qcoeff_out(quant_coeff_out),
        .dequant_out(quant_dequant_out)
    );

    // Inverse quantize module
    wire        iq_done;
    reg         iq_start;
    reg signed [15:0] iq_qcoeff_in;
    reg [15:0] iq_dequant;
    wire signed [15:0] iq_dqcoeff_out;

    av1_inverse_quant u_inv_quant (
        .clk(clk), .rst_n(rst_n),
        .start(iq_start),
        .done(iq_done),
        .qcoeff_in(iq_qcoeff_in),
        .dequant(iq_dequant),
        .dqcoeff_out(iq_dqcoeff_out)
    );

    // Inverse transform module
    wire        ixform_done;
    reg         ixform_start;
    reg signed [15:0] ixform_in [0:7];
    wire signed [15:0] ixform_out_w [0:7];

    av1_inverse_transform u_inv_transform (
        .clk(clk), .rst_n(rst_n),
        .start(ixform_start),
        .is_4x4(1'b0),  // always 8x8 for luma
        .done(ixform_done),
        .in0(ixform_in[0]), .in1(ixform_in[1]),
        .in2(ixform_in[2]), .in3(ixform_in[3]),
        .in4(ixform_in[4]), .in5(ixform_in[5]),
        .in6(ixform_in[6]), .in7(ixform_in[7]),
        .out0(ixform_out_w[0]), .out1(ixform_out_w[1]),
        .out2(ixform_out_w[2]), .out3(ixform_out_w[3]),
        .out4(ixform_out_w[4]), .out5(ixform_out_w[5]),
        .out6(ixform_out_w[6]), .out7(ixform_out_w[7])
    );

    // Intra prediction module
    wire        pred_done;
    reg         pred_start;
    wire [7:0]  pred_out [0:63];

    av1_intra_pred u_intra_pred (
        .clk(clk), .rst_n(rst_n),
        .start(pred_start),
        .is_4x4(1'b0),
        .mode(best_intra_mode),
        .done(pred_done),
        .top(top_pixels), .left(left_pixels),
        .top_left(top_left_pixel),
        .has_top(has_top), .has_left(has_left),
        .pred(pred_out)
    );

    // Motion estimation module
    wire        me_done;
    reg         me_start;
    wire signed [8:0] me_best_mvx, me_best_mvy;
    wire [17:0] me_best_sad;
    wire [19:0] me_ref_rd_addr;

    // Neighbor loading address
    reg [19:0]  neigh_rd_addr;
    reg         neigh_rd_active;

    // Mux reference read address: ME vs neighbor loading
    assign ref_mem_rd_addr = neigh_rd_active ? neigh_rd_addr : me_ref_rd_addr;
    assign ref_rd_is_neigh = neigh_rd_active;

    av1_me #(
        .FRAME_WIDTH(FRAME_WIDTH),
        .FRAME_HEIGHT(FRAME_HEIGHT),
        .SEARCH_RANGE(16)
    ) u_me (
        .clk(clk), .rst_n(rst_n),
        .start(me_start),
        .done(me_done),
        .cur_x({1'b0, blk_x} * 8),
        .cur_y({1'b0, blk_y} * 8),
        .cur_blk(cur_blk),
        .ref_mem_addr(me_ref_rd_addr),
        .ref_mem_data(ref_mem_rd_data),
        .best_mvx(me_best_mvx),
        .best_mvy(me_best_mvy),
        .best_sad(me_best_sad)
    );

    // Entropy coder
    wire        ec_done, ec_busy;
    reg         ec_init, ec_encode_bool, ec_encode_lit, ec_finalize;
    reg         ec_bool_val;
    reg [14:0]  ec_bool_prob;
    reg [7:0]   ec_lit_val;
    reg [4:0]   ec_lit_bits;
    wire        ec_byte_valid;
    wire [7:0]  ec_byte_out;
    wire [23:0] ec_bytes_written;

    av1_entropy u_entropy (
        .clk(clk), .rst_n(rst_n),
        .init(ec_init),
        .encode_bool(ec_encode_bool),
        .encode_lit(ec_encode_lit),
        .finalize(ec_finalize),
        .bool_val(ec_bool_val),
        .bool_prob(ec_bool_prob),
        .lit_val(ec_lit_val),
        .lit_bits(ec_lit_bits),
        .busy(ec_busy),
        .done(ec_done),
        .byte_valid(ec_byte_valid),
        .byte_out(ec_byte_out),
        .bytes_written(ec_bytes_written)
    );

    // Bitstream generator
    wire        bs_done, bs_busy;
    wire        bs_byte_valid;
    wire [7:0]  bs_byte_out;
    wire [23:0] bs_gen_bytes;
    reg         bs_write_td, bs_write_seq, bs_write_frm;

    av1_bitstream #(
        .FRAME_WIDTH(FRAME_WIDTH),
        .FRAME_HEIGHT(FRAME_HEIGHT)
    ) u_bitstream (
        .clk(clk), .rst_n(rst_n),
        .write_td(bs_write_td),
        .write_seq_hdr(bs_write_seq),
        .write_frame_hdr(bs_write_frm),
        .is_keyframe(is_keyframe),
        .qindex(qindex),
        .frame_num(frame_num),
        .busy(bs_busy),
        .done(bs_done),
        .byte_valid(bs_byte_valid),
        .byte_out(bs_byte_out),
        .bytes_written(bs_gen_bytes)
    );

    // ====================================================================
    // Bitstream output mux
    // ====================================================================
    reg [23:0] total_bs_bytes;
    reg [23:0] bs_wr_addr;

    // Mux bitstream and entropy coder output to memory
    assign bs_mem_wr   = bs_byte_valid | ec_byte_valid;
    assign bs_mem_data = bs_byte_valid ? bs_byte_out : ec_byte_out;
    assign bs_mem_addr = bs_wr_addr;
    assign bs_bytes_written = total_bs_bytes;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            total_bs_bytes <= 0;
            bs_wr_addr     <= 0;
        end else begin
            if (top_state == TS_IDLE && start) begin
                total_bs_bytes <= 0;
                bs_wr_addr     <= 0;
            end else if (bs_byte_valid || ec_byte_valid) begin
                bs_wr_addr     <= bs_wr_addr + 1;
                total_bs_bytes <= total_bs_bytes + 1;
            end
        end
    end

    // ====================================================================
    // Main FSM
    // ====================================================================
    integer i;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            top_state <= TS_IDLE;
            done      <= 0;
            blk_x     <= 0;
            blk_y     <= 0;
            fetch_start <= 0;
            pred_start  <= 0;
            xform_start <= 0;
            quant_start <= 0;
            iq_start    <= 0;
            ixform_start <= 0;
            me_start    <= 0;
            ec_init     <= 0;
            ec_encode_bool <= 0;
            ec_encode_lit  <= 0;
            ec_finalize <= 0;
            bs_write_td <= 0;
            bs_write_seq <= 0;
            bs_write_frm <= 0;
            ref_mem_wr_en   <= 0;
            chr_cb_ref_wr_en <= 0;
            chr_cr_ref_wr_en <= 0;
            neigh_rd_active <= 0;
        end else begin
            done <= 0;

            // Clear one-shot signals
            fetch_start  <= 0;
            pred_start   <= 0;
            xform_start  <= 0;
            quant_start  <= 0;
            iq_start     <= 0;
            ixform_start <= 0;
            me_start     <= 0;
            ec_init      <= 0;
            ec_encode_bool <= 0;
            ec_encode_lit  <= 0;
            ec_finalize  <= 0;
            bs_write_td  <= 0;
            bs_write_seq <= 0;
            bs_write_frm <= 0;
            ref_mem_wr_en   <= 0;
            chr_cb_ref_wr_en <= 0;
            chr_cr_ref_wr_en <= 0;

            case (top_state)
                TS_IDLE: begin
                    if (start) begin
                        is_keyframe <= is_keyframe_in;
                        qindex      <= qindex_in;
                        frame_num   <= frame_num_in;
                        blk_x       <= 0;
                        blk_y       <= 0;
                        top_state   <= TS_WRITE_TD;
                    end
                end

                // Write temporal delimiter OBU
                TS_WRITE_TD: begin
                    bs_write_td <= 1;
                    top_state   <= TS_WAIT_TD;
                end
                TS_WAIT_TD: begin
                    if (bs_done) begin
                        top_state <= is_keyframe ? TS_WRITE_SEQ : TS_WRITE_FRM;
                    end
                end

                // Write sequence header (keyframes only)
                TS_WRITE_SEQ: begin
                    bs_write_seq <= 1;
                    top_state    <= TS_WAIT_SEQ;
                end
                TS_WAIT_SEQ: begin
                    if (bs_done) begin
                        top_state <= TS_WRITE_FRM;
                    end
                end

                // Write frame header
                TS_WRITE_FRM: begin
                    bs_write_frm <= 1;
                    ec_init      <= 1;  // Initialize entropy coder
                    top_state    <= TS_WAIT_FRM;
                end
                TS_WAIT_FRM: begin
                    if (bs_done) begin
                        top_state <= TS_FETCH_BLK;
                    end
                end

                // Fetch current 8x8 block
                TS_FETCH_BLK: begin
                    fetch_start     <= 1;
                    fetch_is_chroma <= 0;
                    fetch_chroma_id <= 0;
                    fetch_blk_x     <= blk_x;
                    fetch_blk_y     <= blk_y;
                    top_state       <= TS_WAIT_FETCH;
                end
                TS_WAIT_FETCH: begin
                    if (fetch_done) begin
                        // Copy fetched pixels to cur_blk
                        for (i = 0; i < 64; i = i + 1)
                            cur_blk[i] <= fetch_pixel_buf[i];

                        // Set up neighbor pixels for intra prediction
                        has_top  <= (blk_y > 0);
                        has_left <= (blk_x > 0);
                        // Default neighbor values (overwritten by loader)
                        for (i = 0; i < 8; i = i + 1) begin
                            top_pixels[i]  <= 8'd128;
                            left_pixels[i] <= 8'd128;
                        end
                        top_left_pixel <= 8'd128;

                        // Load actual neighbors from reference frame
                        neigh_cnt <= 0;
                        if (blk_y > 0 || blk_x > 0) begin
                            top_state <= TS_NEIGH_ADDR;
                        end else begin
                            // Block (0,0): no neighbors available
                            if (!is_keyframe)
                                top_state <= TS_ME_START;
                            else begin
                                use_inter <= 0;
                                top_state <= TS_PREDICT;
                            end
                        end
                    end
                end

                // Load neighbor pixels from reference frame
                TS_NEIGH_ADDR: begin
                    neigh_rd_active <= 1;
                    if (neigh_cnt == 5'd0) begin
                        // Top-left pixel
                        if (has_top && has_left) begin
                            neigh_rd_addr <= (blk_y * 8 - 1) * FRAME_WIDTH + blk_x * 8 - 1;
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            neigh_cnt <= 5'd1;  // skip TL, try top
                        end
                    end else if (neigh_cnt <= 5'd8) begin
                        // Top pixels [0..7]
                        if (has_top) begin
                            neigh_rd_addr <= (blk_y * 8 - 1) * FRAME_WIDTH + blk_x * 8 + (neigh_cnt - 5'd1);
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            neigh_cnt <= 5'd9;  // skip top, try left
                        end
                    end else if (neigh_cnt <= 5'd16) begin
                        // Left pixels [0..7]
                        if (has_left) begin
                            neigh_rd_addr <= (blk_y * 8 + (neigh_cnt - 5'd9)) * FRAME_WIDTH + blk_x * 8 - 1;
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            // No left neighbors, done
                            neigh_rd_active <= 0;
                            if (!is_keyframe)
                                top_state <= TS_ME_START;
                            else begin
                                use_inter <= 0;
                                top_state <= TS_PREDICT;
                            end
                        end
                    end
                end

                TS_NEIGH_READ: begin
                    // Store the pixel read from reference memory
                    if (neigh_cnt == 5'd0)
                        top_left_pixel <= ref_mem_rd_data;
                    else if (neigh_cnt <= 5'd8)
                        case (neigh_cnt)
                            5'd1: top_pixels[0] <= ref_mem_rd_data;
                            5'd2: top_pixels[1] <= ref_mem_rd_data;
                            5'd3: top_pixels[2] <= ref_mem_rd_data;
                            5'd4: top_pixels[3] <= ref_mem_rd_data;
                            5'd5: top_pixels[4] <= ref_mem_rd_data;
                            5'd6: top_pixels[5] <= ref_mem_rd_data;
                            5'd7: top_pixels[6] <= ref_mem_rd_data;
                            5'd8: top_pixels[7] <= ref_mem_rd_data;
                            default: ;
                        endcase
                    else
                        case (neigh_cnt)
                            5'd9:  left_pixels[0] <= ref_mem_rd_data;
                            5'd10: left_pixels[1] <= ref_mem_rd_data;
                            5'd11: left_pixels[2] <= ref_mem_rd_data;
                            5'd12: left_pixels[3] <= ref_mem_rd_data;
                            5'd13: left_pixels[4] <= ref_mem_rd_data;
                            5'd14: left_pixels[5] <= ref_mem_rd_data;
                            5'd15: left_pixels[6] <= ref_mem_rd_data;
                            5'd16: left_pixels[7] <= ref_mem_rd_data;
                            default: ;
                        endcase

                    if (neigh_cnt >= 5'd16) begin
                        // All neighbors loaded
                        neigh_rd_active <= 0;
                        if (!is_keyframe)
                            top_state <= TS_ME_START;
                        else begin
                            use_inter <= 0;
                            top_state <= TS_PREDICT;
                        end
                    end else begin
                        neigh_cnt <= neigh_cnt + 1;
                        top_state <= TS_NEIGH_ADDR;
                    end
                end

                // Motion estimation (P-frames)
                TS_ME_START: begin
                    me_start  <= 1;
                    top_state <= TS_WAIT_ME;
                end
                TS_WAIT_ME: begin
                    if (me_done) begin
                        me_mvx <= me_best_mvx;
                        me_mvy <= me_best_mvy;
                        me_sad <= me_best_sad;
                        use_inter <= (me_best_sad < INTRA_SAD_THRESHOLD);
                        top_state <= TS_PREDICT;
                    end
                end

                // Prediction
                TS_PREDICT: begin
                    best_intra_mode <= 3'd0; // DC_PRED for now
                    pred_start <= 1;
                    top_state  <= TS_WAIT_PRED;
                end
                TS_WAIT_PRED: begin
                    if (pred_done) begin
                        // Copy prediction and compute residual
                        for (i = 0; i < 64; i = i + 1) begin
                            pred_blk[i] <= pred_out[i];
                            residual[i] <= $signed({1'b0, cur_blk[i]}) -
                                           $signed({1'b0, pred_out[i]});
                        end
                        xform_row <= 0;
                        top_state <= TS_XFORM_ROW;
                    end
                end

                // Forward transform (row by row, 8 rows of 8)
                TS_XFORM_ROW: begin
                    // Feed one row of residual to transform
                    for (i = 0; i < 8; i = i + 1)
                        xform_in[i] <= residual[xform_row * 8 + i];
                    xform_is_4x4 <= 0;
                    xform_start  <= 1;
                    top_state    <= TS_XFORM_WAIT;
                end

                // Wait for transform done, store output
                TS_XFORM_WAIT: begin
                    if (xform_done) begin
                        for (i = 0; i < 8; i = i + 1)
                            xform_out[xform_row * 8 + i] <= xform_out_w[i];

                        if (xform_row < 7) begin
                            xform_row <= xform_row + 1;
                            top_state <= TS_XFORM_ROW;
                        end else begin
                            // All rows transformed, now do column transforms
                            xform_col <= 0;
                            top_state <= TS_XFORM_COL;
                        end
                    end
                end

                // Forward column transform
                TS_XFORM_COL: begin
                    for (i = 0; i < 8; i = i + 1)
                        xform_in[i] <= xform_out[i * 8 + xform_col];
                    xform_is_4x4 <= 0;
                    xform_start  <= 1;
                    top_state    <= TS_XFORM_COL_WT;
                end

                TS_XFORM_COL_WT: begin
                    if (xform_done) begin
                        for (i = 0; i < 8; i = i + 1)
                            xform_out[i * 8 + xform_col] <= xform_out_w[i];

                        if (xform_col < 7) begin
                            xform_col <= xform_col + 1;
                            top_state <= TS_XFORM_COL;
                        end else begin
                            // All 2D forward transform done, quantize
                            proc_idx  <= 0;
                            top_state <= TS_QCOEFF_START;
                        end
                    end
                end

                // Start quantizer for one coefficient (one-shot pulse)
                TS_QCOEFF_START: begin
                    quant_start    <= 1;
                    quant_is_dc    <= (proc_idx == 0);
                    quant_coeff_in <= xform_out[proc_idx];
                    top_state      <= TS_QCOEFF_WAIT;
                end

                // Wait for quantizer done, then start entropy coding
                TS_QCOEFF_WAIT: begin
                    if (quant_done) begin
                        qcoeff[proc_idx] <= quant_coeff_out;
                        if (proc_idx == 0)
                            dequant_dc <= quant_dequant_out;
                        else if (proc_idx == 1)
                            dequant_ac <= quant_dequant_out;

                        // Entropy code: encode zero/nonzero flag
                        ec_encode_bool <= 1;
                        ec_bool_val    <= (quant_coeff_out != 0) ? 1'b1 : 1'b0;
                        ec_bool_prob   <= 15'd16384;
                        top_state      <= TS_EC_WAIT;
                    end
                end

                // Wait for entropy coder done, advance to next coefficient
                TS_EC_WAIT: begin
                    if (ec_done) begin
                        if (proc_idx < 63) begin
                            proc_idx  <= proc_idx + 1;
                            top_state <= TS_QCOEFF_START;
                        end else begin
                            // All 64 coefficients quantized + entropy coded
                            proc_idx  <= 0;
                            top_state <= TS_IQ_START;
                        end
                    end
                end

                // Start inverse quantizer for one coefficient (one-shot pulse)
                TS_IQ_START: begin
                    iq_start     <= 1;
                    iq_qcoeff_in <= qcoeff[proc_idx];
                    iq_dequant   <= (proc_idx == 0) ? dequant_dc : dequant_ac;
                    top_state    <= TS_IQ_WAIT;
                end

                // Wait for inverse quantizer done
                TS_IQ_WAIT: begin
                    if (iq_done) begin
                        residual[proc_idx] <= iq_dqcoeff_out;
                        if (proc_idx < 63) begin
                            proc_idx  <= proc_idx + 1;
                            top_state <= TS_IQ_START;
                        end else begin
                            // Do inverse column transforms first (2D IDCT)
                            xform_col <= 0;
                            top_state <= TS_IXFORM_COL;
                        end
                    end
                end

                // Inverse column transform (2D IDCT step 1)
                TS_IXFORM_COL: begin
                    for (i = 0; i < 8; i = i + 1)
                        ixform_in[i] <= residual[i * 8 + xform_col];
                    ixform_start <= 1;
                    top_state    <= TS_IXFORM_COL_WT;
                end

                TS_IXFORM_COL_WT: begin
                    if (ixform_done) begin
                        for (i = 0; i < 8; i = i + 1)
                            residual[i * 8 + xform_col] <= ixform_out_w[i];

                        if (xform_col < 7) begin
                            xform_col <= xform_col + 1;
                            top_state <= TS_IXFORM_COL;
                        end else begin
                            // Column IDCT done, now do row IDCT
                            xform_row <= 0;
                            top_state <= TS_IXFORM;
                        end
                    end
                end

                // Inverse row transform (2D IDCT step 2)
                TS_IXFORM: begin
                    for (i = 0; i < 8; i = i + 1)
                        ixform_in[i] <= residual[xform_row * 8 + i];
                    ixform_start <= 1;
                    top_state    <= TS_RECON;
                end

                // Reconstruct pixels
                TS_RECON: begin
                    if (ixform_done) begin
                        // Reconstruct: recon = clamp(pred + round_shift(inv_residual, 4), 0, 255)
                        // The >>4 compensates for the 16x gain from 2D DCT round-trip
                        // (4x per dimension: forward cos_bit=13, inverse cos_bit=12).
                        for (i = 0; i < 8; i = i + 1) begin
                            begin
                                reg signed [16:0] shifted_res;
                                reg signed [16:0] sum;
                                shifted_res = (ixform_out_w[i] + 16'sd8) >>> 4;
                                sum = $signed({1'b0, 8'b0, pred_blk[xform_row * 8 + i]}) +
                                      shifted_res;
                                if (sum < 0)
                                    recon_blk[xform_row * 8 + i] <= 8'd0;
                                else if (sum > 255)
                                    recon_blk[xform_row * 8 + i] <= 8'd255;
                                else
                                    recon_blk[xform_row * 8 + i] <= sum[7:0];
                            end
                        end

                        if (xform_row < 7) begin
                            xform_row <= xform_row + 1;
                            top_state <= TS_IXFORM;
                        end else begin
                            // Write reconstructed block to reference memory
                            ref_wr_idx <= 0;
                            top_state  <= TS_REF_WR;
                        end
                    end
                end

                // Write reconstructed block to reference frame memory
                TS_REF_WR: begin
                    if (ref_wr_idx < 64) begin
                        ref_mem_wr_en   <= 1;
                        ref_mem_wr_addr <= (blk_y * 8 + ref_wr_idx[5:3]) * FRAME_WIDTH +
                                           (blk_x * 8 + ref_wr_idx[2:0]);
                        ref_mem_wr_data <= recon_blk[ref_wr_idx];
                        ref_wr_idx      <= ref_wr_idx + 1;
                    end else begin
                        // Luma done, now process chroma (Cb then Cr)
                        chr_plane <= 0;  // Start with Cb
                        top_state <= TS_CHR_FETCH;
                    end
                end

                // Fetch chroma 4x4 block
                TS_CHR_FETCH: begin
                    fetch_start     <= 1;
                    fetch_is_chroma <= 1;
                    fetch_chroma_id <= chr_plane;
                    fetch_blk_x     <= blk_x;
                    fetch_blk_y     <= blk_y;
                    top_state       <= TS_CHR_WAIT;
                end

                // Wait for chroma fetch, copy to buffer
                TS_CHR_WAIT: begin
                    if (fetch_done) begin
                        for (i = 0; i < 16; i = i + 1)
                            chr_blk[i] <= fetch_pixel_buf[i];
                        chr_wr_idx <= 0;
                        top_state  <= TS_CHR_WR;
                    end
                end

                // Write chroma block to reference memory (passthrough)
                TS_CHR_WR: begin
                    if (chr_wr_idx < 16) begin
                        if (!chr_plane) begin
                            chr_cb_ref_wr_en   <= 1;
                            chr_cb_ref_wr_addr <= (blk_y * 4 + chr_wr_idx[3:2]) * CHROMA_W +
                                                  (blk_x * 4 + chr_wr_idx[1:0]);
                            chr_cb_ref_wr_data <= chr_blk[chr_wr_idx];
                        end else begin
                            chr_cr_ref_wr_en   <= 1;
                            chr_cr_ref_wr_addr <= (blk_y * 4 + chr_wr_idx[3:2]) * CHROMA_W +
                                                  (blk_x * 4 + chr_wr_idx[1:0]);
                            chr_cr_ref_wr_data <= chr_blk[chr_wr_idx];
                        end
                        chr_wr_idx <= chr_wr_idx + 1;
                    end else begin
                        if (!chr_plane) begin
                            // Cb done, now do Cr
                            chr_plane <= 1;
                            top_state <= TS_CHR_FETCH;
                        end else begin
                            // Both chroma planes done
                            top_state <= TS_NEXT_BLK;
                        end
                    end
                end

                // Advance to next block
                TS_NEXT_BLK: begin
                    if (blk_x < BLK_COLS - 1) begin
                        blk_x    <= blk_x + 1;
                        top_state <= TS_FETCH_BLK;
                    end else begin
                        blk_x <= 0;
                        if (blk_y < BLK_ROWS - 1) begin
                            blk_y     <= blk_y + 1;
                            top_state <= TS_FETCH_BLK;
                        end else begin
                            // All blocks processed
                            // Finalize entropy coder
                            ec_finalize <= 1;
                            top_state   <= TS_DONE;
                        end
                    end
                end

                TS_DONE: begin
                    if (ec_done || !ec_busy) begin
                        done      <= 1;
                        top_state <= TS_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
