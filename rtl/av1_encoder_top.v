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
    input  wire        force_intra_in,
    input  wire        dc_only_in,
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
    output wire [23:0] bs_bytes_written,

    // Debug visibility for entropy acceptance tracing
    output wire        ec_dbg_accept_valid_out,
    output wire [1:0]  ec_dbg_accept_kind_out,
    output wire [4:0]  ec_dbg_accept_symbol_out,
    output wire [4:0]  ec_dbg_accept_nsyms_out,
    output wire        ec_dbg_accept_bool_val_out,
    output wire [14:0] ec_dbg_accept_bool_prob_out,
    output wire [255:0] ec_dbg_accept_icdf_flat_out
);

    // ====================================================================
    // Frame geometry
    // ====================================================================
    localparam LUMA_SIZE   = FRAME_WIDTH * FRAME_HEIGHT;
    localparam CHROMA_W    = FRAME_WIDTH / 2;
    localparam CHROMA_H    = FRAME_HEIGHT / 2;
    localparam CHROMA_SIZE = CHROMA_W * CHROMA_H;
    localparam MI_COLS     = FRAME_WIDTH / 4;
    localparam MI_ROWS     = FRAME_HEIGHT / 4;

    // ====================================================================
    // Top-level FSM
    // ====================================================================
    localparam [7:0]
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
        TS_PREDICT_INIT = 6'd34,
        TS_INTER_ADDR   = 6'd35,
        TS_INTER_READ   = 6'd36,
        TS_SYNTAX_SKIP  = 6'd37,
        TS_SYNTAX_WAIT  = 6'd38,
        TS_COEFF_SYM    = 6'd39,
        TS_COEFF_WAIT   = 6'd40,
        TS_SYNTAX_YMODE = 6'd41,
        TS_SYNTAX_YWAIT = 6'd42,
        TS_SYNTAX_ANGLE = 6'd43,
        TS_SYNTAX_AWAIT = 6'd44,
        TS_SYNTAX_UVMODE= 6'd45,
        TS_SYNTAX_UVWAIT= 6'd46,
        TS_TXB_SKIP_Y  = 6'd47,
        TS_TXB_SKIP_YW = 6'd48,
        TS_TXB_SKIP_CB = 6'd49,
        TS_TXB_SKIP_CBW= 6'd50,
        TS_TXB_SKIP_CR = 6'd51,
        TS_TXB_SKIP_CRW= 6'd52,
        TS_DC_TX_TYPE  = 6'd53,
        TS_DC_TX_WAIT  = 6'd54,
        TS_DC_EOB      = 6'd55,
        TS_DC_EOB_WAIT = 6'd56,
        TS_DC_BASE     = 6'd57,
        TS_DC_BASE_WAIT= 6'd58,
        TS_DC_SIGN     = 6'd59,
        TS_DC_SIGN_WAIT= 6'd60,
        TS_DC_BR       = 6'd61,
        TS_DC_BR_WAIT  = 6'd62,
        TS_AC01_TX_TYPE    = 7'd63,
        TS_AC01_TX_WAIT    = 7'd64,
        TS_AC01_EOB        = 7'd65,
        TS_AC01_EOB_WAIT   = 7'd66,
        TS_AC01_EOB_EXTRA  = 7'd67,
        TS_AC01_EOB_EXWAIT = 7'd68,
        TS_AC01_AC_BASE    = 7'd69,
        TS_AC01_AC_WAIT    = 7'd70,
        TS_AC01_SCAN1_BASE = 7'd71,
        TS_AC01_SCAN1_WAIT = 7'd72,
        TS_AC01_DC_BASE    = 7'd73,
        TS_AC01_DC_WAIT    = 7'd74,
        TS_AC01_SIGN_DC    = 7'd75,
        TS_AC01_SIGN_DCW   = 7'd76,
        TS_AC01_SIGN_AC    = 7'd77,
        TS_AC01_SIGN_ACW   = 7'd78,
        TS_AC09_TX_TYPE    = 7'd79,
        TS_AC09_TX_WAIT    = 7'd80,
        TS_AC09_EOB        = 7'd81,
        TS_AC09_EOB_WAIT   = 7'd82,
        TS_AC09_EOB_EXTRA  = 7'd83,
        TS_AC09_EOB_EXWAIT = 7'd84,
        TS_AC09_EOB_BIT1   = 7'd85,
        TS_AC09_EOB_BIT1W  = 7'd86,
        TS_AC09_EOB_BIT0   = 7'd87,
        TS_AC09_EOB_BIT0W  = 7'd88,
        TS_AC09_BASE10     = 7'd89,
        TS_AC09_BASE10W    = 7'd90,
        TS_AC09_BASE17     = 7'd91,
        TS_AC09_BASE17W    = 7'd92,
        TS_AC09_BASE24     = 7'd93,
        TS_AC09_BASE24W    = 7'd94,
        TS_AC09_BASE16     = 7'd95,
        TS_AC09_BASE16W    = 7'd96,
        TS_AC09_BASE9      = 7'd97,
        TS_AC09_BASE9W     = 7'd98,
        TS_AC09_BASE2      = 7'd99,
        TS_AC09_BASE2W     = 7'd100,
        TS_AC09_BASE1      = 7'd101,
        TS_AC09_BASE1W     = 7'd102,
        TS_AC09_BASE8      = 7'd103,
        TS_AC09_BASE8W     = 7'd104,
        TS_AC09_DC_BASE    = 7'd105,
        TS_AC09_DC_WAIT    = 7'd106,
        TS_AC09_SIGN_DC    = 7'd107,
        TS_AC09_SIGN_DCW   = 7'd108,
        TS_AC09_SIGN_AC8   = 7'd109,
        TS_AC09_SIGN_AC8W  = 7'd110,
        TS_AC09_SIGN_AC1   = 7'd111,
        TS_AC09_SIGN_AC1W  = 7'd112,
        TS_AC09_SIGN_AC10  = 7'd113,
        TS_AC09_SIGN_AC10W = 7'd114,
        TS_AC01_BR         = 7'd115,
        TS_AC01_BR_WAIT    = 7'd116,
        TS_GEN_TX_TYPE     = 7'd117,
        TS_GEN_TX_WAIT     = 7'd118,
        TS_GEN_EOB         = 7'd119,
        TS_GEN_EOB_WAIT    = 7'd120,
        TS_GEN_EOB_EXTRA   = 7'd121,
        TS_GEN_EOB_EXWAIT  = 7'd122,
        TS_GEN_EOB_BIT     = 7'd123,
        TS_GEN_EOB_BITW    = 7'd124,
        TS_GEN_BASE        = 7'd125,
        TS_GEN_BASEW       = 7'd126,
        TS_GEN_BR          = 7'd127,
        TS_GEN_BRW         = 8'd128,
        TS_GEN_SIGN        = 8'd129,
        TS_GEN_SIGNW       = 8'd130,
        TS_PART_PREP       = 8'd131,
        TS_PART_EMIT       = 8'd132,
        TS_PART_WAIT       = 8'd133,
        TS_DONE_COMMIT     = 8'd134,
        TS_GEN_GOLOMB_ZERO = 8'd135,
        TS_GEN_GOLOMB_ZW   = 8'd136,
        TS_GEN_GOLOMB_BIT  = 8'd137,
        TS_GEN_GOLOMB_BW   = 8'd138,
        TS_DONE_FINISH     = 8'd139,
        TS_SYNTAX_II       = 8'd140,
        TS_SYNTAX_IIWAIT   = 8'd141,
        TS_SYNTAX_REF1     = 8'd142,
        TS_SYNTAX_REF1W    = 8'd143,
        TS_SYNTAX_REF2     = 8'd144,
        TS_SYNTAX_REF2W    = 8'd145,
        TS_SYNTAX_REF3     = 8'd146,
        TS_SYNTAX_REF3W    = 8'd147,
        TS_SYNTAX_NEWMV    = 8'd148,
        TS_SYNTAX_NEWMVW   = 8'd149,
        TS_SYNTAX_ZEROMV   = 8'd150,
        TS_SYNTAX_ZEROMVW  = 8'd151,
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

    reg [7:0]  top_state;
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
    reg [7:0]  top_right_pixels [0:7];
    reg [7:0]  left_pixels [0:7];
    reg [7:0]  bottom_left_pixels [0:7];
    reg [7:0]  top_left_pixel;
    reg        has_top, has_left;
    reg        has_top_right, has_bottom_left;

    // Motion estimation results
    reg signed [8:0] me_mvx, me_mvy;
    reg [17:0] me_sad;
    reg        use_inter;  // 1 if P-frame and ME result is good
    reg signed [11:0] inter_base_x, inter_base_y;
    reg [5:0]  inter_fetch_idx;

    // SAD threshold for choosing inter vs intra
    localparam [17:0] INTRA_SAD_THRESHOLD = 18'd4000;

    // Intra mode selection
    localparam [3:0]
        AV1_DC_PRED     = 4'd0,
        AV1_V_PRED      = 4'd1,
        AV1_H_PRED      = 4'd2,
        AV1_D45_PRED    = 4'd3,
        AV1_D135_PRED   = 4'd4,
        AV1_D113_PRED   = 4'd5,
        AV1_D157_PRED   = 4'd6,
        AV1_D203_PRED   = 4'd7,
        AV1_D67_PRED    = 4'd8,
        AV1_SMOOTH_PRED = 4'd9,
        AV1_PAETH_PRED  = 4'd12;

    reg [3:0] best_intra_mode /* verilator public_flat */;
    reg [3:0] intra_eval_idx;
    reg [17:0] intra_best_sad;
    reg [17:0] intra_cand_sad;
    reg        cur_block_has_coeff;

    // Tile/block syntax context state mirrored from the software writer.
    // The top-level does not fully emit these symbols yet, but it now tracks
    // the neighborhood state needed for partition/skip/mode ownership work.
    reg [7:0]  part_ctx_above [0:MI_COLS-1];
    reg [7:0]  part_ctx_left  [0:MI_ROWS-1];
    reg        skip_above     [0:MI_COLS-1];
    reg        skip_left      [0:MI_ROWS-1];
    reg        inter_above    [0:MI_COLS-1];
    reg        inter_left     [0:MI_ROWS-1];
    reg [2:0]  ref_above      [0:MI_COLS-1];
    reg [2:0]  ref_left       [0:MI_ROWS-1];
    reg [3:0]  mode_above     [0:MI_COLS-1];
    reg [3:0]  mode_left      [0:MI_ROWS-1];
    reg        blk_inter_coded[0:BLK_COLS*BLK_ROWS-1];
    reg [2:0]  blk_ref0       [0:BLK_COLS*BLK_ROWS-1];
    reg [1:0]  blk_inter_mode [0:BLK_COLS*BLK_ROWS-1];
    reg [1:0]  dc_sign_above  [0:MI_COLS-1];
    reg [1:0]  dc_sign_left   [0:MI_ROWS-1];
    reg        cur_only_dc_nonzero;
    reg        cur_only_reduced_ac_nonzero;
    reg        cur_only_eob9_nonzero;
    reg        cur_all_coeffs_le_14;
    reg [4:0]  dc_br_remaining;
    reg [4:0]  coeff_br_remaining;
    reg        coeff_br_capped;
    reg [3:0]  coeff_eob_bit_idx;
    reg [4:0]  golomb_zero_remaining;
    reg [4:0]  golomb_bit_idx;
    reg [15:0] golomb_x;
    reg [1:0]  part_stage;
    reg [2:0]  part_level_log2;
    reg [4:0]  part_symbol;
    reg [4:0]  part_nsyms;

    localparam [2:0]
        REF_LAST    = 3'd0,
        REF_LAST2   = 3'd1,
        REF_LAST3   = 3'd2,
        REF_GOLDEN  = 3'd3,
        REF_BWDREF  = 3'd4,
        REF_ALTREF2 = 3'd5,
        REF_ALTREF  = 3'd6,
        REF_NONE    = 3'd7;

    localparam [1:0]
        REDUCED_INTER_NONE     = 2'd0,
        REDUCED_INTER_GLOBALMV = 2'd1,
        REDUCED_INTER_NEWMV    = 2'd2;

    localparam integer AV1_GLOBALMV_OFFSET = 3;
    localparam integer AV1_REFMV_OFFSET = 4;

    function [3:0] intra_mode_from_idx;
        input [3:0] idx;
        begin
            case (idx)
                4'd0: intra_mode_from_idx = AV1_DC_PRED;
                4'd1: intra_mode_from_idx = AV1_V_PRED;
                4'd2: intra_mode_from_idx = AV1_H_PRED;
                4'd3: intra_mode_from_idx = AV1_D45_PRED;
                4'd4: intra_mode_from_idx = AV1_D135_PRED;
                4'd5: intra_mode_from_idx = AV1_D113_PRED;
                4'd6: intra_mode_from_idx = AV1_D157_PRED;
                4'd7: intra_mode_from_idx = AV1_D203_PRED;
                4'd8: intra_mode_from_idx = AV1_D67_PRED;
                4'd9: intra_mode_from_idx = AV1_SMOOTH_PRED;
                default: intra_mode_from_idx = AV1_PAETH_PRED;
            endcase
        end
    endfunction

    function [1:0] get_skip_ctx_cur;
        input [9:0] cur_blk_x;
        input [9:0] cur_blk_y;
        integer mi_col;
        integer mi_row;
        integer ctx;
        begin
            mi_col = cur_blk_x << 1;
            mi_row = cur_blk_y << 1;
            ctx = 0;
            if (mi_row > 0 && mi_col < MI_COLS)
                ctx = ctx + skip_above[mi_col];
            if (mi_col > 0 && mi_row < MI_ROWS)
                ctx = ctx + skip_left[mi_row];
            get_skip_ctx_cur = ctx[1:0];
        end
    endfunction

    function [255:0] skip_icdf_flat;
        input [1:0] ctx;
        begin
            case (ctx)
                2'd0: skip_icdf_flat = {224'd0, 16'd0, 16'd1097};
                2'd1: skip_icdf_flat = {224'd0, 16'd0, 16'd16253};
                default: skip_icdf_flat = {224'd0, 16'd0, 16'd28192};
            endcase
        end
    endfunction

    function [1:0] get_intra_inter_ctx_cur;
        input [9:0] cur_blk_x;
        input [9:0] cur_blk_y;
        integer mi_col;
        integer mi_row;
        integer has_above;
        integer has_left;
        integer above_intra;
        integer left_intra;
        begin
            mi_col = cur_blk_x << 1;
            mi_row = cur_blk_y << 1;
            has_above = (cur_blk_y > 0) && (mi_col < MI_COLS);
            has_left = (cur_blk_x > 0) && (mi_row < MI_ROWS);
            above_intra = has_above ? !inter_above[mi_col] : 0;
            left_intra = has_left ? !inter_left[mi_row] : 0;
            if (has_above && has_left) begin
                if (left_intra && above_intra)
                    get_intra_inter_ctx_cur = 2'd3;
                else if (left_intra || above_intra)
                    get_intra_inter_ctx_cur = 2'd1;
                else
                    get_intra_inter_ctx_cur = 2'd0;
            end else if (has_above || has_left) begin
                get_intra_inter_ctx_cur = (has_above ? above_intra : left_intra) ? 2'd2 : 2'd0;
            end else begin
                get_intra_inter_ctx_cur = 2'd0;
            end
        end
    endfunction

    function [255:0] intra_inter_icdf_flat;
        input [1:0] ctx;
        begin
            case (ctx)
                2'd0: intra_inter_icdf_flat = {224'd0, 16'd0, 16'd31962};
                2'd1: intra_inter_icdf_flat = {224'd0, 16'd0, 16'd16106};
                2'd2: intra_inter_icdf_flat = {224'd0, 16'd0, 16'd12582};
                default: intra_inter_icdf_flat = {224'd0, 16'd0, 16'd6230};
            endcase
        end
    endfunction

    function [1:0] compare_ref_counts_fn;
        input integer a;
        input integer b;
        begin
            if (a == b)
                compare_ref_counts_fn = 2'd1;
            else
                compare_ref_counts_fn = (a < b) ? 2'd0 : 2'd2;
        end
    endfunction

    function ref_is_forward_fn;
        input [2:0] ref_frame;
        begin
            case (ref_frame)
                REF_LAST,
                REF_LAST2,
                REF_LAST3,
                REF_GOLDEN: ref_is_forward_fn = 1'b1;
                default:    ref_is_forward_fn = 1'b0;
            endcase
        end
    endfunction

    function ref_is_backward_fn;
        input [2:0] ref_frame;
        begin
            case (ref_frame)
                REF_BWDREF,
                REF_ALTREF2,
                REF_ALTREF: ref_is_backward_fn = 1'b1;
                default:    ref_is_backward_fn = 1'b0;
            endcase
        end
    endfunction

    function block_has_matching_ref_fn;
        input integer blk_x_in;
        input integer blk_y_in;
        input [2:0] ref_frame;
        integer blk_idx_fn;
        begin
            if (blk_x_in < 0 || blk_y_in < 0 || blk_x_in >= BLK_COLS || blk_y_in >= BLK_ROWS) begin
                block_has_matching_ref_fn = 1'b0;
            end else begin
                blk_idx_fn = blk_y_in * BLK_COLS + blk_x_in;
                block_has_matching_ref_fn =
                    blk_inter_coded[blk_idx_fn] && (blk_ref0[blk_idx_fn] == ref_frame);
            end
        end
    endfunction

    function block_uses_newmv_fn;
        input integer blk_x_in;
        input integer blk_y_in;
        input [2:0] ref_frame;
        integer blk_idx_fn;
        begin
            if (!block_has_matching_ref_fn(blk_x_in, blk_y_in, ref_frame)) begin
                block_uses_newmv_fn = 1'b0;
            end else begin
                blk_idx_fn = blk_y_in * BLK_COLS + blk_x_in;
                block_uses_newmv_fn = (blk_inter_mode[blk_idx_fn] == REDUCED_INTER_NEWMV);
            end
        end
    endfunction

    function block_has_top_right_fn;
        input integer blk_x_in;
        input integer blk_y_in;
        integer bs;
        integer mask_row;
        integer mask_col;
        reg has_tr;
        begin
            if (blk_x_in < 0 || blk_y_in < 0 || blk_x_in >= BLK_COLS || blk_y_in >= BLK_ROWS) begin
                block_has_top_right_fn = 1'b0;
            end else begin
                bs = 1;
                mask_row = blk_y_in & 7;
                mask_col = blk_x_in & 7;
                has_tr = !((mask_row & bs) && (mask_col & bs));
                while (bs < 8) begin
                    if (mask_col & bs) begin
                        if ((mask_col & (2 * bs)) && (mask_row & (2 * bs))) begin
                            has_tr = 1'b0;
                            bs = 8;
                        end else begin
                            bs = bs << 1;
                        end
                    end else begin
                        bs = 8;
                    end
                end
                block_has_top_right_fn = has_tr && (blk_y_in > 0) && (blk_x_in + 1 < BLK_COLS);
            end
        end
    endfunction

    function block_has_row_match_fn;
        input integer blk_x_in;
        input integer blk_y_in;
        input [2:0] ref_frame;
        input integer include_top_right;
        integer dy;
        begin
            block_has_row_match_fn = 1'b0;
            if (block_has_matching_ref_fn(blk_x_in, blk_y_in - 1, ref_frame))
                block_has_row_match_fn = 1'b1;
            else if (include_top_right && block_has_matching_ref_fn(blk_x_in + 1, blk_y_in - 1, ref_frame))
                block_has_row_match_fn = 1'b1;
            else if (block_has_matching_ref_fn(blk_x_in - 1, blk_y_in - 1, ref_frame))
                block_has_row_match_fn = 1'b1;
            else begin
                for (dy = 2; dy <= 4; dy = dy + 1)
                    if (block_has_matching_ref_fn(blk_x_in, blk_y_in - dy, ref_frame))
                        block_has_row_match_fn = 1'b1;
            end
        end
    endfunction

    function block_has_col_match_fn;
        input integer blk_x_in;
        input integer blk_y_in;
        input [2:0] ref_frame;
        integer dx;
        begin
            block_has_col_match_fn = 1'b0;
            if (block_has_matching_ref_fn(blk_x_in - 1, blk_y_in, ref_frame))
                block_has_col_match_fn = 1'b1;
            else if (block_has_matching_ref_fn(blk_x_in - 1, blk_y_in - 1, ref_frame))
                block_has_col_match_fn = 1'b1;
            else begin
                for (dx = 2; dx <= 4; dx = dx + 1)
                    if (block_has_matching_ref_fn(blk_x_in - dx, blk_y_in, ref_frame))
                        block_has_col_match_fn = 1'b1;
            end
        end
    endfunction

    function [7:0] get_reduced_single_ref_mode_ctx_fn;
        input integer blk_x_in;
        input integer blk_y_in;
        input [2:0] ref_frame;
        integer has_tr;
        integer row_match;
        integer col_match;
        integer newmv_count;
        integer row_ref_match;
        integer col_ref_match;
        integer nearest_match;
        integer ref_match;
        begin
            has_tr = block_has_top_right_fn(blk_x_in, blk_y_in);
            row_match = block_has_matching_ref_fn(blk_x_in, blk_y_in - 1, ref_frame) ||
                        (has_tr && block_has_matching_ref_fn(blk_x_in + 1, blk_y_in - 1, ref_frame));
            col_match = block_has_matching_ref_fn(blk_x_in - 1, blk_y_in, ref_frame);
            newmv_count = 0;
            if (block_uses_newmv_fn(blk_x_in, blk_y_in - 1, ref_frame))
                newmv_count = newmv_count + 1;
            if (has_tr && block_uses_newmv_fn(blk_x_in + 1, blk_y_in - 1, ref_frame))
                newmv_count = newmv_count + 1;
            if (block_uses_newmv_fn(blk_x_in - 1, blk_y_in, ref_frame))
                newmv_count = newmv_count + 1;

            row_ref_match = block_has_row_match_fn(blk_x_in, blk_y_in, ref_frame, has_tr);
            col_ref_match = block_has_col_match_fn(blk_x_in, blk_y_in, ref_frame);
            nearest_match = row_match + col_match;
            ref_match = row_ref_match + col_ref_match;
            get_reduced_single_ref_mode_ctx_fn = 8'd0;
            case (nearest_match)
                0: begin
                    get_reduced_single_ref_mode_ctx_fn[2:0] = ref_match >= 1 ? 3'd1 : 3'd0;
                    if (ref_match == 1)
                        get_reduced_single_ref_mode_ctx_fn[7:4] = 4'd1;
                    else if (ref_match >= 2)
                        get_reduced_single_ref_mode_ctx_fn[7:4] = 4'd2;
                end
                1: begin
                    get_reduced_single_ref_mode_ctx_fn[2:0] = newmv_count > 0 ? 3'd2 : 3'd3;
                    if (ref_match == 1)
                        get_reduced_single_ref_mode_ctx_fn[7:4] = 4'd3;
                    else if (ref_match >= 2)
                        get_reduced_single_ref_mode_ctx_fn[7:4] = 4'd4;
                end
                default: begin
                    get_reduced_single_ref_mode_ctx_fn[2:0] = newmv_count >= 1 ? 3'd4 : 3'd5;
                    get_reduced_single_ref_mode_ctx_fn[7:4] = 4'd5;
                end
            endcase
        end
    endfunction

    function [255:0] single_ref_icdf_flat;
        input [1:0] cmp_ctx;
        input [1:0] branch_sel;
        begin
            case ({cmp_ctx, branch_sel})
                4'd0: single_ref_icdf_flat = {224'd0, 16'd0, 16'd27871};
                4'd1: single_ref_icdf_flat = {224'd0, 16'd0, 16'd15795};
                4'd2: single_ref_icdf_flat = {224'd0, 16'd0, 16'd18781};
                4'd4: single_ref_icdf_flat = {224'd0, 16'd0, 16'd15795};
                4'd5: single_ref_icdf_flat = {224'd0, 16'd0, 16'd16017};
                4'd6: single_ref_icdf_flat = {224'd0, 16'd0, 16'd12921};
                4'd8: single_ref_icdf_flat = {224'd0, 16'd0, 16'd5024};
                4'd9: single_ref_icdf_flat = {224'd0, 16'd0, 16'd4489};
                4'd10:single_ref_icdf_flat = {224'd0, 16'd0, 16'd1274};
                default: single_ref_icdf_flat = {224'd0, 16'd0, 16'd15795};
            endcase
        end
    endfunction

    function [255:0] newmv_icdf_flat;
        input [2:0] ctx;
        begin
            case (ctx)
                3'd0: newmv_icdf_flat = {224'd0, 16'd0, 16'd8733};
                3'd1: newmv_icdf_flat = {224'd0, 16'd0, 16'd16138};
                3'd2: newmv_icdf_flat = {224'd0, 16'd0, 16'd17429};
                3'd3: newmv_icdf_flat = {224'd0, 16'd0, 16'd24382};
                3'd4: newmv_icdf_flat = {224'd0, 16'd0, 16'd20546};
                default: newmv_icdf_flat = {224'd0, 16'd0, 16'd28092};
            endcase
        end
    endfunction

    function [255:0] zeromv_icdf_flat;
        input [1:0] ctx;
        begin
            case (ctx)
                2'd0: zeromv_icdf_flat = {224'd0, 16'd0, 16'd30593};
                default: zeromv_icdf_flat = {224'd0, 16'd0, 16'd31714};
            endcase
        end
    endfunction

    function [4:0] get_partition_ctx_cur;
        input [13:0] org_x;
        input [13:0] org_y;
        input [1:0]  bsl;
        integer mi_col;
        integer mi_row;
        integer above;
        integer left;
        begin
            mi_col = org_x >> 2;
            mi_row = org_y >> 2;
            above = 0;
            left = 0;
            if ((mi_row > 0) && (mi_col < MI_COLS))
                above = (part_ctx_above[mi_col] >> bsl) & 1;
            if ((mi_col > 0) && (mi_row < MI_ROWS))
                left = (part_ctx_left[mi_row] >> bsl) & 1;
            get_partition_ctx_cur = (bsl << 2) + (left << 1) + above;
        end
    endfunction

    function [255:0] partition_icdf_flat;
        input [4:0] ctx;
        begin
            case (ctx)
                5'd0:  partition_icdf_flat = {192'd0,16'd0,16'd2376,16'd7258,16'd13636};
                5'd1:  partition_icdf_flat = {192'd0,16'd0,16'd4228,16'd12913,16'd18840};
                5'd2:  partition_icdf_flat = {192'd0,16'd0,16'd4139,16'd9089,16'd20246};
                5'd3:  partition_icdf_flat = {192'd0,16'd0,16'd6915,16'd13985,16'd22872};
                5'd4:  partition_icdf_flat = {96'd0,16'd0,16'd866,16'd2197,16'd3167,16'd3947,16'd5104,16'd6062,16'd8197,16'd11839,16'd17171};
                5'd5:  partition_icdf_flat = {96'd0,16'd0,16'd2934,16'd4067,16'd6117,16'd7725,16'd8797,16'd10298,16'd15983,16'd21725,16'd24843};
                5'd6:  partition_icdf_flat = {96'd0,16'd0,16'd651,16'd6432,16'd7231,16'd8268,16'd10408,16'd12280,16'd17657,16'd19499,16'd27354};
                5'd7:  partition_icdf_flat = {96'd0,16'd0,16'd1597,16'd4939,16'd6332,16'd7990,16'd9715,16'd11908,16'd24154,16'd26406,16'd30106};
                5'd8:  partition_icdf_flat = {96'd0,16'd0,16'd1224,16'd2590,16'd3249,16'd3719,16'd4541,16'd5121,16'd9644,16'd11848,16'd14306};
                5'd9:  partition_icdf_flat = {96'd0,16'd0,16'd3716,16'd4727,16'd5817,16'd6586,16'd7108,16'd7776,16'd20712,16'd23708,16'd25079};
                5'd10: partition_icdf_flat = {96'd0,16'd0,16'd721,16'd5242,16'd5697,16'd6223,16'd7359,16'd8224,16'd22706,16'd23759,16'd26753};
                5'd11: partition_icdf_flat = {96'd0,16'd0,16'd869,16'd2583,16'd2928,16'd3302,16'd3707,16'd4154,16'd29972,16'd30560,16'd31374};
                5'd12: partition_icdf_flat = {96'd0,16'd0,16'd1044,16'd1876,16'd2244,16'd2507,16'd2931,16'd3202,16'd9690,16'd11221,16'd12631};
                5'd13: partition_icdf_flat = {96'd0,16'd0,16'd1664,16'd2138,16'd2799,16'd3253,16'd3518,16'd3824,16'd14824,16'd25278,16'd26036};
                5'd14: partition_icdf_flat = {96'd0,16'd0,16'd530,16'd2470,16'd2704,16'd3019,16'd3651,16'd4085,16'd24420,16'd25105,16'd26823};
                5'd15: partition_icdf_flat = {96'd0,16'd0,16'd436,16'd887,16'd1025,16'd1194,16'd1374,16'd1570,16'd31281,16'd31556,16'd31898};
                5'd16: partition_icdf_flat = {128'd0,16'd0,16'd129,16'd149,16'd229,16'd284,16'd4239,16'd4549,16'd4869};
                5'd17: partition_icdf_flat = {128'd0,16'd0,16'd397,16'd430,16'd549,16'd708,16'd2708,16'd25778,16'd26161};
                5'd18: partition_icdf_flat = {128'd0,16'd0,16'd186,16'd237,16'd541,16'd741,16'd711,16'd26092,16'd27339};
                default: partition_icdf_flat = {128'd0,16'd0,16'd104,16'd151,16'd230,16'd320,16'd31296,16'd31802,16'd32057};
            endcase
        end
    endfunction

    function [2:0] intra_mode_context_from_mode;
        input [3:0] mode;
        begin
            case (mode)
                AV1_V_PRED:      intra_mode_context_from_mode = 3'd1;
                AV1_H_PRED:      intra_mode_context_from_mode = 3'd2;
                AV1_D45_PRED:    intra_mode_context_from_mode = 3'd3;
                AV1_D135_PRED:   intra_mode_context_from_mode = 3'd4;
                AV1_D113_PRED:   intra_mode_context_from_mode = 3'd4;
                AV1_D157_PRED:   intra_mode_context_from_mode = 3'd4;
                AV1_D203_PRED:   intra_mode_context_from_mode = 3'd4;
                AV1_D67_PRED:    intra_mode_context_from_mode = 3'd3;
                AV1_SMOOTH_PRED: intra_mode_context_from_mode = 3'd0;
                AV1_PAETH_PRED:  intra_mode_context_from_mode = 3'd0;
                default:         intra_mode_context_from_mode = 3'd0;
            endcase
        end
    endfunction

    function is_directional_mode;
        input [3:0] mode;
        begin
            case (mode)
                AV1_V_PRED,
                AV1_H_PRED,
                AV1_D45_PRED,
                AV1_D135_PRED,
                AV1_D113_PRED,
                AV1_D157_PRED,
                AV1_D203_PRED,
                AV1_D67_PRED: is_directional_mode = 1'b1;
                default:      is_directional_mode = 1'b0;
            endcase
        end
    endfunction

    function [2:0] get_kf_mode_above_ctx_cur;
        input [9:0] cur_blk_x;
        input [9:0] cur_blk_y;
        integer mi_col;
        begin
            mi_col = cur_blk_x << 1;
            if ((cur_blk_y > 0) && (mi_col < MI_COLS))
                get_kf_mode_above_ctx_cur = intra_mode_context_from_mode(mode_above[mi_col]);
            else
                get_kf_mode_above_ctx_cur = 3'd0;
        end
    endfunction

    function [2:0] get_kf_mode_left_ctx_cur;
        input [9:0] cur_blk_x;
        input [9:0] cur_blk_y;
        integer mi_row;
        begin
            mi_row = cur_blk_y << 1;
            if ((cur_blk_x > 0) && (mi_row < MI_ROWS))
                get_kf_mode_left_ctx_cur = intra_mode_context_from_mode(mode_left[mi_row]);
            else
                get_kf_mode_left_ctx_cur = 3'd0;
        end
    endfunction

    function [255:0] kf_y_mode_icdf_flat;
        input [2:0] above_ctx;
        input [2:0] left_ctx;
        begin
            case ({above_ctx, left_ctx})
                6'd0:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd2302,16'd3675,16'd4603,16'd8579,16'd9524,16'd10943,16'd11658,16'd12086,16'd12550,16'd13430,16'd15741,16'd17180};
                6'd1:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd1359,16'd2596,16'd4110,16'd8334,16'd9736,16'd10880,16'd11324,16'd12049,16'd12465,16'd13252,16'd14702,16'd20752};
                6'd2:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd1201,16'd2839,16'd3432,16'd6608,16'd7148,16'd8635,16'd9529,16'd9713,16'd9980,16'd10472,16'd21997,16'd22716};
                6'd3:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd306,16'd1810,16'd3183,16'd8022,16'd10672,16'd12770,16'd13222,16'd13632,16'd13960,16'd16326,16'd17362,16'd18677};
                6'd4:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd293,16'd1695,16'd2507,16'd6331,16'd7185,16'd10377,16'd12735,16'd14159,16'd16267,16'd17165,16'd19503,16'd20646};
                6'd8:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd1332,16'd2286,16'd3754,16'd7387,16'd8745,16'd9679,16'd10008,16'd10936,16'd11328,16'd11920,16'd13183,16'd22745};
                6'd9:  kf_y_mode_icdf_flat = {48'd0,16'd0,16'd974,16'd1492,16'd2863,16'd5158,16'd6345,16'd6855,16'd6973,16'd7702,16'd7882,16'd8208,16'd8669,16'd26785};
                6'd10: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd2276,16'd3678,16'd4853,16'd8299,16'd9363,16'd10598,16'd11161,16'd11691,16'd12040,16'd12591,16'd19987,16'd25324};
                6'd11: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd558,16'd1672,16'd3615,16'd8119,16'd12943,16'd14360,16'd14596,16'd15360,16'd15681,16'd17336,16'd18079,16'd24231};
                6'd12: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd774,16'd1787,16'd3093,16'd6767,16'd8252,16'd10784,16'd12051,16'd14863,16'd16573,16'd17272,16'd18537,16'd25225};
                6'd16: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd691,16'd2463,16'd3230,16'd7039,16'd7713,16'd9367,16'd10191,16'd10456,16'd10764,16'd11385,16'd19177,16'd20155};
                6'd17: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd1861,16'd3557,16'd5025,16'd9549,16'd10706,16'd12073,16'd12621,16'd13164,16'd13538,16'd14262,16'd19298,16'd23081};
                6'd18: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd682,16'd1974,16'd2301,16'd4213,16'd4414,16'd5686,16'd6334,16'd6402,16'd6516,16'd6744,16'd26263,16'd26585};
                6'd19: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd516,16'd2481,16'd3793,16'd8890,16'd11245,16'd14207,16'd14844,16'd15203,16'd15544,16'd17814,16'd21034,16'd22050};
                6'd20: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd305,16'd2031,16'd2696,16'd6207,16'd6807,16'd11205,16'd13597,16'd14344,16'd15505,16'd16267,16'd22910,16'd23574};
                6'd24: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd359,16'd1851,16'd3072,16'd7708,16'd11349,16'd13044,16'd13453,16'd13990,16'd14387,16'd17280,16'd18369,16'd20166};
                6'd25: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd426,16'd1610,16'd3283,16'd7543,16'd13300,16'd14364,16'd14637,16'd15329,16'd15663,16'd18244,16'd18947,16'd24565};
                6'd26: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd750,16'd2690,16'd3650,16'd8163,16'd11230,16'd13698,16'd14343,16'd14756,16'd15125,16'd17764,16'd23037,16'd24317};
                6'd27: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd240,16'd1232,16'd2379,16'd6025,16'd14001,16'd15615,16'd15774,16'd15951,16'd16101,16'd23252,16'd23720,16'd25054};
                6'd28: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd283,16'd1785,16'd2815,16'd6999,16'd10050,16'd13660,16'd14825,16'd16116,16'd17451,16'd21272,16'd22488,16'd23925};
                6'd32: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd291,16'd1618,16'd2554,16'd6549,16'd7319,16'd9779,16'd11855,16'd13693,16'd15934,16'd16789,16'd19097,16'd20190};
                6'd33: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd491,16'd1625,16'd3115,16'd7388,16'd8532,16'd10561,16'd11905,16'd15012,16'd16876,16'd17688,16'd19142,16'd23205};
                6'd34: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd507,16'd2245,16'd2868,16'd6302,16'd6821,16'd10170,16'd12662,16'd13418,16'd14512,16'd15152,16'd23867,16'd24412};
                6'd35: kf_y_mode_icdf_flat = {48'd0,16'd0,16'd286,16'd1885,16'd3279,16'd8153,16'd10015,16'd13821,16'd14729,16'd15750,16'd16726,16'd19644,16'd20953,16'd21933};
                default:kf_y_mode_icdf_flat = {48'd0,16'd0,16'd175,16'd966,16'd1413,16'd3588,16'd3992,16'd9865,16'd14111,16'd17382,16'd22259,16'd22909,16'd24480,16'd25150};
            endcase
        end
    endfunction

    function [255:0] angle_delta_icdf_flat;
        input [3:0] mode;
        begin
            case (mode)
                AV1_V_PRED:    angle_delta_icdf_flat = {192'd0,16'd9992,16'd25201,16'd27736,16'd30588};
                AV1_H_PRED:    angle_delta_icdf_flat = {192'd0,16'd9281,16'd23967,16'd27160,16'd30467};
                AV1_D45_PRED:  angle_delta_icdf_flat = {192'd0,16'd13414,16'd19069,16'd21750,16'd28988};
                AV1_D135_PRED: angle_delta_icdf_flat = {192'd0,16'd15630,16'd17621,16'd21542,16'd28187};
                AV1_D113_PRED: angle_delta_icdf_flat = {192'd0,16'd13180,16'd18259,16'd21841,16'd31031};
                AV1_D157_PRED: angle_delta_icdf_flat = {192'd0,16'd15118,16'd20283,16'd22592,16'd30104};
                AV1_D203_PRED: angle_delta_icdf_flat = {192'd0,16'd12427,16'd17315,16'd21672,16'd30528};
                default:       angle_delta_icdf_flat = {192'd0,16'd15092,16'd20309,16'd22340,16'd29163};
            endcase
        end
    endfunction

    function [255:0] if_y_mode_icdf_flat;
        begin
            if_y_mode_icdf_flat = {48'd0,16'd0,16'd1916,16'd2784,16'd3067,16'd4616,16'd5404,16'd7241,16'd8119,16'd8818,16'd9450,16'd10137,16'd12923,16'd14095};
        end
    endfunction

    function [255:0] uv_mode_dc_icdf_flat;
        input [3:0] y_mode;
        begin
            case (y_mode)
                AV1_DC_PRED:     uv_mode_dc_icdf_flat = {240'd0,16'd22361};
                AV1_V_PRED:      uv_mode_dc_icdf_flat = {240'd0,16'd28236};
                AV1_H_PRED:      uv_mode_dc_icdf_flat = {240'd0,16'd27495};
                AV1_D45_PRED:    uv_mode_dc_icdf_flat = {240'd0,16'd26028};
                AV1_D135_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd27781};
                AV1_D113_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd27398};
                AV1_D157_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd27952};
                AV1_D203_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd26160};
                AV1_D67_PRED:    uv_mode_dc_icdf_flat = {240'd0,16'd26770};
                AV1_SMOOTH_PRED: uv_mode_dc_icdf_flat = {240'd0,16'd22108};
                AV1_PAETH_PRED:  uv_mode_dc_icdf_flat = {240'd0,16'd29624};
                default:         uv_mode_dc_icdf_flat = {240'd0,16'd22361};
            endcase
        end
    endfunction

    function [255:0] txb_skip_luma_icdf_flat;
        begin
            txb_skip_luma_icdf_flat = {224'd0,16'd0,16'd865};
        end
    endfunction

    function [255:0] txb_skip_chroma_icdf_flat;
        begin
            txb_skip_chroma_icdf_flat = {224'd0,16'd0,16'd30055};
        end
    endfunction

    function [255:0] intra_tx_type_dct_icdf_flat;
        input [3:0] mode;
        begin
            case (mode)
                4'd0:    intra_tx_type_dct_icdf_flat = {224'd0,16'd19026,16'd30898};
                4'd1:    intra_tx_type_dct_icdf_flat = {224'd0,16'd23972,16'd32442};
                4'd2:    intra_tx_type_dct_icdf_flat = {224'd0,16'd25192,16'd32284};
                4'd3:    intra_tx_type_dct_icdf_flat = {224'd0,16'd17428,16'd31642};
                4'd4:    intra_tx_type_dct_icdf_flat = {224'd0,16'd27914,16'd32113};
                4'd5:    intra_tx_type_dct_icdf_flat = {224'd0,16'd26310,16'd31469};
                4'd6:    intra_tx_type_dct_icdf_flat = {224'd0,16'd27473,16'd32457};
                4'd7:    intra_tx_type_dct_icdf_flat = {224'd0,16'd24709,16'd31885};
                4'd8:    intra_tx_type_dct_icdf_flat = {224'd0,16'd25188,16'd32027};
                4'd9:    intra_tx_type_dct_icdf_flat = {224'd0,16'd25362,16'd32658};
                4'd10:   intra_tx_type_dct_icdf_flat = {224'd0,16'd24794,16'd32405};
                4'd11:   intra_tx_type_dct_icdf_flat = {224'd0,16'd25121,16'd32615};
                4'd12:   intra_tx_type_dct_icdf_flat = {224'd0,16'd26436,16'd29257};
                default: intra_tx_type_dct_icdf_flat = {224'd0,16'd19026,16'd30898};
            endcase
        end
    endfunction

    function [255:0] eob_multi64_luma_icdf_flat;
        begin
            eob_multi64_luma_icdf_flat = {144'd0,16'd0,16'd4903,16'd10215,16'd16410,16'd20708,16'd25227,16'd26461};
        end
    endfunction

    function [255:0] eob_extra_ctx0_icdf_flat;
        begin
            eob_extra_ctx0_icdf_flat = {224'd0,16'd0,16'd12530};
        end
    endfunction

    function [255:0] coeff_base_eob_ctx0_icdf_flat;
        begin
            coeff_base_eob_ctx0_icdf_flat = {208'd0,16'd0,16'd1725,16'd11311};
        end
    endfunction

    function [255:0] coeff_base_eob_ctx1_icdf_flat;
        begin
            coeff_base_eob_ctx1_icdf_flat = {208'd0,16'd0,16'd285,16'd817};
        end
    endfunction

    function [255:0] coeff_base_ctx0_icdf_flat;
        begin
            coeff_base_ctx0_icdf_flat = {192'd0,16'd0,16'd10626,16'd15820,16'd25014};
        end
    endfunction

    function [255:0] coeff_base_ctx1_icdf_flat;
        begin
            coeff_base_ctx1_icdf_flat = {192'd0,16'd0,16'd77,16'd438,16'd7098};
        end
    endfunction

    function [255:0] coeff_base_ctx2_icdf_flat;
        begin
            coeff_base_ctx2_icdf_flat = {192'd0,16'd0,16'd774,16'd3543,16'd17105};
        end
    endfunction

    function [255:0] coeff_base_ctx6_icdf_flat;
        begin
            coeff_base_ctx6_icdf_flat = {192'd0,16'd0,16'd43,16'd173,16'd5206};
        end
    endfunction

    function [255:0] coeff_base_ctx7_icdf_flat;
        begin
            coeff_base_ctx7_icdf_flat = {192'd0,16'd0,16'd369,16'd2180,16'd15193};
        end
    endfunction

    function [255:0] dc_sign_ctx0_icdf_flat;
        input [1:0] ctx;
        begin
            case (ctx)
                2'd1:    dc_sign_ctx0_icdf_flat = {224'd0,16'd0,16'd19712};
                2'd2:    dc_sign_ctx0_icdf_flat = {224'd0,16'd0,16'd13952};
                default: dc_sign_ctx0_icdf_flat = {224'd0,16'd0,16'd16768};
            endcase
        end
    endfunction

    function [255:0] coeff_br_ctx0_icdf_flat;
        begin
            coeff_br_ctx0_icdf_flat = {192'd0,16'd0,16'd4878,16'd7955,16'd14494};
        end
    endfunction

    function [255:0] coeff_br_ctx1_icdf_flat;
        begin
            coeff_br_ctx1_icdf_flat = {192'd0,16'd0,16'd5765,16'd9619,16'd17231};
        end
    endfunction

    function [255:0] eob_extra_ctx2_icdf_flat;
        begin
            eob_extra_ctx2_icdf_flat = {224'd0,16'd0,16'd13609};
        end
    endfunction

`include "av1_tx8x8_coeff_helpers.vh"
`include "av1_tx8x8_qctx_tables.vh"

    function [3:0] clip_max3_fn;
        input [15:0] val;
        begin
            if (val > 16'd3)
                clip_max3_fn = 4'd3;
            else
                clip_max3_fn = {1'b0, val[2:0]};
        end
    endfunction

    function [15:0] coeff_abs_level_qcoeff_flat;
        input [5:0] pos;
        begin
            coeff_abs_level_qcoeff_flat = abs16(qcoeff[pos]);
        end
    endfunction

    function [15:0] coeff_abs_level_qcoeff_rc;
        input integer row;
        input integer col;
        begin
            if ((row < 0) || (row > 7) || (col < 0) || (col > 7))
                coeff_abs_level_qcoeff_rc = 16'd0;
            else
                coeff_abs_level_qcoeff_rc = coeff_abs_level_qcoeff_flat((row << 3) + col);
        end
    endfunction

    function [3:0] coeff_clipped_level_qcoeff_rc;
        input integer row;
        input integer col;
        reg [15:0] level;
        begin
            if ((row < 0) || (row > 7) || (col < 0) || (col > 7)) begin
                coeff_clipped_level_qcoeff_rc = 4'd0;
            end else begin
                level = coeff_abs_level_qcoeff_flat((row << 3) + col);
                coeff_clipped_level_qcoeff_rc = clip_max3_fn(level);
            end
        end
    endfunction

    function [5:0] get_nz_map_ctx_qcoeff;
        input [5:0] pos;
        integer row;
        integer col;
        integer stats;
        integer ctx;
        begin
            if (pos == 6'd0) begin
                get_nz_map_ctx_qcoeff = 6'd0;
            end else begin
                row = pos[5:3];
                col = pos[2:0];
                stats =
                    coeff_clipped_level_qcoeff_rc(row,     col + 1) +
                    coeff_clipped_level_qcoeff_rc(row + 1, col    ) +
                    coeff_clipped_level_qcoeff_rc(row + 1, col + 1) +
                    coeff_clipped_level_qcoeff_rc(row,     col + 2) +
                    coeff_clipped_level_qcoeff_rc(row + 2, col    );
                ctx = ((stats + 1) >> 1);
                if (ctx > 4)
                    ctx = 4;
                get_nz_map_ctx_qcoeff = nz_map_ctx_offset_8x8_fn(pos) + ctx;
            end
        end
    endfunction

    function [4:0] get_br_ctx_qcoeff;
        input [5:0] pos;
        integer row;
        integer col;
        integer mag;
        begin
            row = pos[5:3];
            col = pos[2:0];
            mag =
                coeff_abs_level_qcoeff_rc(row,     col + 1) +
                coeff_abs_level_qcoeff_rc(row + 1, col    ) +
                coeff_abs_level_qcoeff_rc(row + 1, col + 1);
            mag = ((mag + 1) >> 1);
            if (mag > 6)
                mag = 6;
            if (pos == 6'd0)
                get_br_ctx_qcoeff = mag;
            else if ((row < 2) && (col < 2))
                get_br_ctx_qcoeff = mag + 5'd7;
            else
                get_br_ctx_qcoeff = mag + 5'd14;
        end
    endfunction

    function [3:0] eob_to_pos_small_fn;
        input [5:0] eob;
        begin
            case (eob)
                6'd0:  eob_to_pos_small_fn = 4'd0;
                6'd1:  eob_to_pos_small_fn = 4'd1;
                6'd2:  eob_to_pos_small_fn = 4'd2;
                6'd3, 6'd4: eob_to_pos_small_fn = 4'd3;
                6'd5, 6'd6, 6'd7, 6'd8: eob_to_pos_small_fn = 4'd4;
                6'd9, 6'd10, 6'd11, 6'd12, 6'd13, 6'd14, 6'd15, 6'd16:
                    eob_to_pos_small_fn = 4'd5;
                default: eob_to_pos_small_fn = 4'd6;
            endcase
        end
    endfunction

    function [3:0] get_eob_pos_token_fn;
        input [6:0] eob;
        integer idx;
        begin
            if (eob < 7'd33) begin
                get_eob_pos_token_fn = eob_to_pos_small_fn(eob[5:0]);
            end else begin
                idx = (eob - 1) >> 5;
                case (idx)
                    0: get_eob_pos_token_fn = 4'd6;
                    1: get_eob_pos_token_fn = 4'd7;
                    2: get_eob_pos_token_fn = 4'd8;
                    3: get_eob_pos_token_fn = 4'd9;
                    4: get_eob_pos_token_fn = 4'd10;
                    default: get_eob_pos_token_fn = 4'd11;
                endcase
            end
        end
    endfunction

    function [9:0] eob_group_start_fn;
        input [3:0] pt;
        begin
            case (pt)
                4'd0: eob_group_start_fn = 10'd0;
                4'd1: eob_group_start_fn = 10'd1;
                4'd2: eob_group_start_fn = 10'd2;
                4'd3: eob_group_start_fn = 10'd3;
                4'd4: eob_group_start_fn = 10'd5;
                4'd5: eob_group_start_fn = 10'd9;
                4'd6: eob_group_start_fn = 10'd17;
                4'd7: eob_group_start_fn = 10'd33;
                4'd8: eob_group_start_fn = 10'd65;
                4'd9: eob_group_start_fn = 10'd129;
                4'd10: eob_group_start_fn = 10'd257;
                default: eob_group_start_fn = 10'd513;
            endcase
        end
    endfunction

    function [3:0] eob_offset_bits_fn;
        input [3:0] pt;
        begin
            case (pt)
                4'd0, 4'd1, 4'd2: eob_offset_bits_fn = 4'd0;
                4'd3:  eob_offset_bits_fn = 4'd1;
                4'd4:  eob_offset_bits_fn = 4'd2;
                4'd5:  eob_offset_bits_fn = 4'd3;
                4'd6:  eob_offset_bits_fn = 4'd4;
                4'd7:  eob_offset_bits_fn = 4'd5;
                4'd8:  eob_offset_bits_fn = 4'd6;
                4'd9:  eob_offset_bits_fn = 4'd7;
                4'd10: eob_offset_bits_fn = 4'd8;
                default: eob_offset_bits_fn = 4'd9;
            endcase
        end
    endfunction

    function [6:0] compute_eob_qcoeff;
        integer c;
        reg [6:0] eob;
        begin
            eob = 7'd0;
            for (c = 0; c < 64; c = c + 1) begin
                if (qcoeff[scan_8x8_pos(c)] != 16'sd0)
                    eob = c + 7'd1;
            end
            compute_eob_qcoeff = eob;
        end
    endfunction

    function [9:0] get_eob_extra_fn;
        input [6:0] eob;
        input [3:0] pt;
        begin
            get_eob_extra_fn = eob - eob_group_start_fn(pt);
        end
    endfunction

    function [4:0] bit_length16_fn;
        input [15:0] val;
        integer tmp;
        begin
            bit_length16_fn = 5'd0;
            tmp = val;
            while (tmp > 0) begin
                bit_length16_fn = bit_length16_fn + 5'd1;
                tmp = tmp >> 1;
            end
            if (bit_length16_fn == 5'd0)
                bit_length16_fn = 5'd1;
        end
    endfunction

    function [2:0] coeff_base_eob_ctx_from_scan_fn;
        input [6:0] scan_idx;
        begin
            if (scan_idx == 7'd0)
                coeff_base_eob_ctx_from_scan_fn = 3'd0;
            else if (scan_idx <= 7'd8)
                coeff_base_eob_ctx_from_scan_fn = 3'd1;
            else if (scan_idx <= 7'd16)
                coeff_base_eob_ctx_from_scan_fn = 3'd2;
            else
                coeff_base_eob_ctx_from_scan_fn = 3'd3;
        end
    endfunction

    function integer dc_sign_delta;
        input [1:0] code;
        begin
            case (code)
                2'd1:    dc_sign_delta = -1;
                2'd2:    dc_sign_delta = 1;
                default: dc_sign_delta = 0;
            endcase
        end
    endfunction

    function [1:0] get_dc_sign_ctx_cur;
        input [9:0] cur_blk_x;
        input [9:0] cur_blk_y;
        integer mi_col;
        integer mi_row;
        integer acc;
        integer t;
        begin
            mi_col = cur_blk_x << 1;
            mi_row = cur_blk_y << 1;
            acc = 0;
            for (t = 0; t < 2; t = t + 1) begin
                if ((cur_blk_y > 0) && ((mi_col + t) < MI_COLS))
                    acc = acc + dc_sign_delta(dc_sign_above[mi_col + t]);
                if ((cur_blk_x > 0) && ((mi_row + t) < MI_ROWS))
                    acc = acc + dc_sign_delta(dc_sign_left[mi_row + t]);
            end
            if (acc > 0)
                get_dc_sign_ctx_cur = 2'd2;
            else if (acc < 0)
                get_dc_sign_ctx_cur = 2'd1;
            else
                get_dc_sign_ctx_cur = 2'd0;
        end
    endfunction

    function [15:0] abs16;
        input signed [15:0] val;
        begin
            abs16 = val[15] ? -val : val;
        end
    endfunction

    function [4:0] coeff_base_eob_sym_from_level;
        input [15:0] level;
        begin
            if (level <= 16'd1)
                coeff_base_eob_sym_from_level = 5'd0;
            else if (level == 16'd2)
                coeff_base_eob_sym_from_level = 5'd1;
            else
                coeff_base_eob_sym_from_level = 5'd2;
        end
    endfunction

    function [5:0] morton3_from_xy;
        input [2:0] x;
        input [2:0] y;
        begin
            morton3_from_xy = {y[2], x[2], y[1], x[1], y[0], x[0]};
        end
    endfunction

    function [2:0] morton3_to_x;
        input [5:0] morton;
        begin
            morton3_to_x = {morton[4], morton[2], morton[0]};
        end
    endfunction

    function [2:0] morton3_to_y;
        input [5:0] morton;
        begin
            morton3_to_y = {morton[5], morton[3], morton[1]};
        end
    endfunction

    function [20:0] next_blk_morton_packed;
        input [9:0] cur_blk_x_in;
        input [9:0] cur_blk_y_in;
        integer sbx_scan;
        integer sby_scan;
        integer morton_scan;
        integer start_morton;
        integer cur_sbx;
        integer cur_sby;
        integer cand_x;
        integer cand_y;
        reg found;
        begin
            cur_sbx = cur_blk_x_in >> 3;
            cur_sby = cur_blk_y_in >> 3;
            next_blk_morton_packed = 21'd0;
            found = 1'b0;
            for (sby_scan = cur_sby; sby_scan < SB_ROWS; sby_scan = sby_scan + 1) begin
                for (sbx_scan = 0; sbx_scan < SB_COLS; sbx_scan = sbx_scan + 1) begin
                    if (!found &&
                        ((sby_scan > cur_sby) || ((sby_scan == cur_sby) && (sbx_scan >= cur_sbx)))) begin
                        if ((sby_scan == cur_sby) && (sbx_scan == cur_sbx))
                            start_morton = morton3_from_xy(cur_blk_x_in[2:0], cur_blk_y_in[2:0]) + 1;
                        else
                            start_morton = 0;
                        for (morton_scan = start_morton; morton_scan < 64; morton_scan = morton_scan + 1) begin
                            cand_x = (sbx_scan << 3) + morton3_to_x(morton_scan[5:0]);
                            cand_y = (sby_scan << 3) + morton3_to_y(morton_scan[5:0]);
                            if (!found && (cand_x < BLK_COLS) && (cand_y < BLK_ROWS)) begin
                                next_blk_morton_packed = {1'b1, cand_y[9:0], cand_x[9:0]};
                                found = 1'b1;
                            end
                        end
                    end
                end
            end
        end
    endfunction

    function signed [15:0] round_shift16;
        input signed [15:0] val;
        input integer shift;
        reg signed [16:0] biased;
        begin
            if (shift <= 0)
                round_shift16 = val;
            else begin
                biased = $signed({val[15], val}) + $signed(17'sd1 <<< (shift - 1));
                round_shift16 = biased >>> shift;
            end
        end
    endfunction

    wire [1:0] cur_skip_ctx = get_skip_ctx_cur(blk_x, blk_y);
    wire [1:0] cur_intra_inter_ctx = get_intra_inter_ctx_cur(blk_x, blk_y);
    wire       cur_block_skip = ~cur_block_has_coeff;
    wire [2:0] cur_kf_above_ctx = get_kf_mode_above_ctx_cur(blk_x, blk_y);
    wire [2:0] cur_kf_left_ctx  = get_kf_mode_left_ctx_cur(blk_x, blk_y);
    wire [255:0] cur_kf_y_icdf  = kf_y_mode_icdf_flat(cur_kf_above_ctx, cur_kf_left_ctx);
    wire [255:0] cur_if_y_icdf  = if_y_mode_icdf_flat();
    wire [255:0] cur_ang_icdf   = angle_delta_icdf_flat(best_intra_mode);
    wire [255:0] cur_uv_icdf    = uv_mode_dc_icdf_flat(best_intra_mode);
    wire [1:0]   cur_coeff_qctx = coeff_qctx_from_qindex_fn(qindex);
    wire [255:0] cur_txb_luma_icdf = txb_skip_luma_icdf_flat_qctx(cur_coeff_qctx, 4'd0);
    wire [255:0] cur_txb_chr_icdf  = txb_skip_chroma_icdf_flat_qctx(cur_coeff_qctx, 4'd7);
    wire [255:0] cur_intra_tx_icdf = intra_tx_type_dct_icdf_flat(best_intra_mode);
    wire [255:0] cur_eob_multi_icdf = eob_multi64_icdf_flat_qctx(cur_coeff_qctx, 1'b0);
    wire [255:0] cur_eob_extra_icdf = eob_extra_ctx_icdf_flat_qctx(cur_coeff_qctx, 1'b0, 4'd0);
    wire [255:0] cur_base_eob_icdf = coeff_base_eob_ctx_icdf_flat_qctx(cur_coeff_qctx, 3'd0);
    wire [255:0] cur_base_eob1_icdf= coeff_base_eob_ctx_icdf_flat_qctx(cur_coeff_qctx, 3'd1);
    wire [255:0] cur_base0_icdf    = coeff_base_ctx_icdf_flat_qctx(cur_coeff_qctx, 6'd0);
    wire [255:0] cur_base1_icdf    = coeff_base_ctx_icdf_flat_qctx(cur_coeff_qctx, 6'd1);
    wire [255:0] cur_base2_icdf    = coeff_base_ctx_icdf_flat_qctx(cur_coeff_qctx, 6'd2);
    wire [255:0] cur_base6_icdf    = coeff_base_ctx_icdf_flat_qctx(cur_coeff_qctx, 6'd6);
    wire [255:0] cur_base7_icdf    = coeff_base_ctx_icdf_flat_qctx(cur_coeff_qctx, 6'd7);
    wire [1:0]   cur_dc_sign_ctx   = get_dc_sign_ctx_cur(blk_x, blk_y);
    wire [255:0] cur_dc_sign_icdf  = dc_sign_ctx0_icdf_flat(cur_dc_sign_ctx);
    wire [255:0] cur_coeff_br_icdf = coeff_br_ctx_icdf_flat_qctx(cur_coeff_qctx, 5'd0);
    wire [255:0] cur_coeff_br1_icdf = coeff_br_ctx_icdf_flat_qctx(cur_coeff_qctx, 5'd1);
    wire [255:0] cur_eob_extra2_icdf = eob_extra_ctx_icdf_flat_qctx(cur_coeff_qctx, 1'b0, 4'd2);
    wire [1:0]   cur_dc_sign_code =
        cur_block_has_coeff ? (qcoeff[0][15] ? 2'd1 : (qcoeff[0] != 16'sd0 ? 2'd2 : 2'd0)) : 2'd0;
    wire         cur_dc_only_coeff_path =
        cur_block_has_coeff && cur_only_dc_nonzero && (abs16(qcoeff[0]) <= 16'd14);
    wire         cur_scan01_coeff_path =
        cur_block_has_coeff &&
        cur_only_reduced_ac_nonzero &&
        !cur_only_dc_nonzero &&
        (qcoeff[0] != 16'sd0) &&
        (qcoeff[1] != 16'sd0) &&
        (qcoeff[8] == 16'sd0) &&
        (abs16(qcoeff[0]) <= 16'd3) &&
        (abs16(qcoeff[1]) == 16'd1);
    wire         cur_eob9_coeff_path =
        cur_block_has_coeff &&
        cur_only_eob9_nonzero &&
        !cur_only_dc_nonzero &&
        (abs16(qcoeff[0]) == 16'd1) &&
        (abs16(qcoeff[8]) == 16'd1) &&
        (abs16(qcoeff[1]) == 16'd2) &&
        (abs16(qcoeff[10]) == 16'd1);
    wire [6:0]   cur_generic_eob = compute_eob_qcoeff();
    wire [3:0]   cur_generic_eob_pt = get_eob_pos_token_fn(cur_generic_eob);
    wire [9:0]   cur_generic_eob_extra = get_eob_extra_fn(cur_generic_eob, cur_generic_eob_pt);
    wire [3:0]   cur_generic_eob_bits = eob_offset_bits_fn(cur_generic_eob_pt);
    wire [20:0]  next_blk_morton = next_blk_morton_packed(blk_x, blk_y);
    wire         has_next_blk_morton = next_blk_morton[20];
    wire [9:0]   next_blk_y_morton = next_blk_morton[19:10];
    wire [9:0]   next_blk_x_morton = next_blk_morton[9:0];
    wire         cur_generic_coeff_path =
        cur_block_has_coeff &&
        !use_inter;

    wire [3:0] intra_eval_mode = intra_mode_from_idx(intra_eval_idx);

    // Processing counters
    reg [5:0]  proc_idx;
    reg [2:0]  xform_row;
    reg [2:0]  xform_col;
    reg [8:0]  ref_wr_idx;
    reg [5:0]  neigh_cnt;    // 0=TL, 1-8=top, 9-16=left, 17-24=top-right, 25-32=bottom-left

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
        .mode(intra_eval_mode),
        .done(pred_done),
        .top(top_pixels), .left(left_pixels),
        .top_right(top_right_pixels), .bottom_left(bottom_left_pixels),
        .top_left(top_left_pixel),
        .has_top(has_top), .has_left(has_left),
        .has_top_right(has_top_right), .has_bottom_left(has_bottom_left),
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
    reg [19:0]  inter_rd_addr;
    reg         inter_rd_active;

    // Mux reference read address: neighbor load vs inter prediction vs ME
    assign ref_mem_rd_addr = neigh_rd_active ? neigh_rd_addr :
                             inter_rd_active ? inter_rd_addr :
                             me_ref_rd_addr;
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
    reg         ec_init, ec_encode_bool, ec_encode_lit, ec_encode_symbol, ec_finalize;
    reg         ec_bool_val;
    reg [14:0]  ec_bool_prob;
    reg [7:0]   ec_lit_val;
    reg [4:0]   ec_lit_bits;
    reg [4:0]   ec_symbol;
    reg [4:0]   ec_nsyms;
    reg [255:0] ec_icdf_flat;
    wire        ec_byte_valid;
    wire [7:0]  ec_byte_out;
    wire [23:0] ec_bytes_written;
    wire        ec_dbg_accept_valid;
    wire [1:0]  ec_dbg_accept_kind;
    wire [4:0]  ec_dbg_accept_symbol;
    wire [4:0]  ec_dbg_accept_nsyms;
    wire        ec_dbg_accept_bool_val;
    wire [14:0] ec_dbg_accept_bool_prob;
    wire [255:0] ec_dbg_accept_icdf_flat;

    av1_entropy u_entropy (
        .clk(clk), .rst_n(rst_n),
        .init(ec_init),
        .encode_bool(ec_encode_bool),
        .encode_lit(ec_encode_lit),
        .encode_symbol(ec_encode_symbol),
        .finalize(ec_finalize),
        .bool_val(ec_bool_val),
        .bool_prob(ec_bool_prob),
        .lit_val(ec_lit_val),
        .lit_bits(ec_lit_bits),
        .symbol(ec_symbol),
        .nsyms(ec_nsyms),
        .icdf_flat(ec_icdf_flat),
        .busy(ec_busy),
        .done(ec_done),
        .byte_valid(ec_byte_valid),
        .byte_out(ec_byte_out),
        .bytes_written(ec_bytes_written),
        .dbg_accept_valid(ec_dbg_accept_valid),
        .dbg_accept_kind(ec_dbg_accept_kind),
        .dbg_accept_symbol(ec_dbg_accept_symbol),
        .dbg_accept_nsyms(ec_dbg_accept_nsyms),
        .dbg_accept_bool_val(ec_dbg_accept_bool_val),
        .dbg_accept_bool_prob(ec_dbg_accept_bool_prob),
        .dbg_accept_icdf_flat(ec_dbg_accept_icdf_flat)
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
    reg [23:0] bs_mem_addr_r;
    reg [7:0]  bs_mem_data_r;
    reg        bs_mem_wr_r;
    reg        manual_bs_wr;
    reg [23:0] manual_bs_addr;
    reg [7:0]  manual_bs_data;
    reg [23:0] frame_obu_start_addr;
    wire [23:0] frame_obu_payload_bytes =
        (total_bs_bytes > (frame_obu_start_addr + 24'd2)) ?
            (total_bs_bytes - frame_obu_start_addr - 24'd2) : 24'd0;

    // Register bitstream writes so address/data stay aligned to the same
    // sampled source-valid event.
    assign bs_mem_wr   = bs_mem_wr_r;
    assign bs_mem_data = bs_mem_data_r;
    assign bs_mem_addr = bs_mem_addr_r;
    assign bs_bytes_written = total_bs_bytes;
    assign ec_dbg_accept_valid_out = ec_dbg_accept_valid;
    assign ec_dbg_accept_kind_out = ec_dbg_accept_kind;
    assign ec_dbg_accept_symbol_out = ec_dbg_accept_symbol;
    assign ec_dbg_accept_nsyms_out = ec_dbg_accept_nsyms;
    assign ec_dbg_accept_bool_val_out = ec_dbg_accept_bool_val;
    assign ec_dbg_accept_bool_prob_out = ec_dbg_accept_bool_prob;
    assign ec_dbg_accept_icdf_flat_out = ec_dbg_accept_icdf_flat;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            total_bs_bytes <= 0;
            bs_wr_addr     <= 0;
            bs_mem_addr_r  <= 0;
            bs_mem_data_r  <= 0;
            bs_mem_wr_r    <= 0;
        end else begin
            bs_mem_wr_r <= 0;
            if (top_state == TS_IDLE && start) begin
                total_bs_bytes <= 0;
                bs_wr_addr     <= 0;
            end else if (manual_bs_wr) begin
                bs_mem_wr_r   <= 1'b1;
                bs_mem_addr_r <= manual_bs_addr;
                bs_mem_data_r <= manual_bs_data;
            end else if (bs_byte_valid) begin
                bs_mem_wr_r    <= 1'b1;
                bs_mem_addr_r  <= bs_wr_addr;
                bs_mem_data_r  <= bs_byte_out;
                bs_wr_addr     <= bs_wr_addr + 1;
                total_bs_bytes <= total_bs_bytes + 1;
            end else if (ec_byte_valid) begin
                bs_mem_wr_r    <= 1'b1;
                bs_mem_addr_r  <= bs_wr_addr;
                bs_mem_data_r  <= ec_byte_out;
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
            ec_encode_symbol <= 0;
            ec_finalize <= 0;
            bs_write_td <= 0;
            bs_write_seq <= 0;
            bs_write_frm <= 0;
            ref_mem_wr_en   <= 0;
            chr_cb_ref_wr_en <= 0;
            chr_cr_ref_wr_en <= 0;
            neigh_rd_active <= 0;
            inter_rd_active <= 0;
            manual_bs_wr <= 0;
            ec_symbol <= 5'd0;
            ec_nsyms <= 5'd0;
            ec_icdf_flat <= 256'd0;
            cur_block_has_coeff <= 1'b0;
            cur_only_dc_nonzero <= 1'b1;
            cur_only_reduced_ac_nonzero <= 1'b1;
            cur_only_eob9_nonzero <= 1'b1;
            cur_all_coeffs_le_14 <= 1'b1;
            dc_br_remaining <= 5'd0;
            coeff_br_remaining <= 5'd0;
            coeff_br_capped <= 1'b0;
            coeff_eob_bit_idx <= 4'd0;
            golomb_zero_remaining <= 5'd0;
            golomb_bit_idx <= 5'd0;
            golomb_x <= 16'd0;
            part_stage <= 2'd0;
            part_level_log2 <= 3'd0;
            part_symbol <= 5'd0;
            part_nsyms <= 5'd0;
            best_intra_mode <= AV1_DC_PRED;
            intra_eval_idx  <= 4'd0;
            intra_best_sad  <= 18'h3FFFF;
            intra_cand_sad  <= 18'd0;
            frame_obu_start_addr <= 24'd0;
            for (i = 0; i < MI_COLS; i = i + 1) begin
                part_ctx_above[i] <= 8'd0;
                skip_above[i] <= 1'b0;
                inter_above[i] <= 1'b0;
                ref_above[i] <= REF_NONE;
                mode_above[i] <= AV1_DC_PRED;
                dc_sign_above[i] <= 2'd0;
            end
            for (i = 0; i < MI_ROWS; i = i + 1) begin
                part_ctx_left[i] <= 8'd0;
                skip_left[i] <= 1'b0;
                inter_left[i] <= 1'b0;
                ref_left[i] <= REF_NONE;
                mode_left[i] <= AV1_DC_PRED;
                dc_sign_left[i] <= 2'd0;
            end
            for (i = 0; i < (BLK_COLS * BLK_ROWS); i = i + 1) begin
                blk_inter_coded[i] <= 1'b0;
                blk_ref0[i] <= REF_NONE;
                blk_inter_mode[i] <= REDUCED_INTER_NONE;
            end
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
            ec_encode_symbol <= 0;
            ec_finalize  <= 0;
            bs_write_td  <= 0;
            bs_write_seq <= 0;
            bs_write_frm <= 0;
            ref_mem_wr_en   <= 0;
            chr_cb_ref_wr_en <= 0;
            chr_cr_ref_wr_en <= 0;
            manual_bs_wr <= 0;
            ec_symbol <= 5'd0;
            ec_nsyms <= 5'd0;
            ec_icdf_flat <= 256'd0;

            case (top_state)
                TS_IDLE: begin
                    if (start) begin
                        is_keyframe <= is_keyframe_in;
                        qindex      <= (qindex_in == 8'd0) ? 8'd1 : qindex_in;
                        frame_num   <= frame_num_in;
                        blk_x       <= 0;
                        blk_y       <= 0;
                        cur_block_has_coeff <= 1'b0;
                        cur_only_dc_nonzero <= 1'b1;
                        cur_only_reduced_ac_nonzero <= 1'b1;
                        cur_only_eob9_nonzero <= 1'b1;
                        cur_all_coeffs_le_14 <= 1'b1;
                        dc_br_remaining <= 5'd0;
                        coeff_br_remaining <= 5'd0;
                        coeff_br_capped <= 1'b0;
                        coeff_eob_bit_idx <= 4'd0;
                        golomb_zero_remaining <= 5'd0;
                        golomb_bit_idx <= 5'd0;
                        golomb_x <= 16'd0;
                        part_stage <= 2'd0;
                        part_level_log2 <= 3'd0;
                        part_symbol <= 5'd0;
                        part_nsyms <= 5'd0;
                        for (i = 0; i < MI_COLS; i = i + 1) begin
                            part_ctx_above[i] <= 8'd0;
                            skip_above[i] <= 1'b0;
                            inter_above[i] <= 1'b0;
                            ref_above[i] <= REF_NONE;
                            mode_above[i] <= AV1_DC_PRED;
                            dc_sign_above[i] <= 2'd0;
                        end
                        for (i = 0; i < MI_ROWS; i = i + 1) begin
                            part_ctx_left[i] <= 8'd0;
                            skip_left[i] <= 1'b0;
                            inter_left[i] <= 1'b0;
                            ref_left[i] <= REF_NONE;
                            mode_left[i] <= AV1_DC_PRED;
                            dc_sign_left[i] <= 2'd0;
                        end
                        for (i = 0; i < (BLK_COLS * BLK_ROWS); i = i + 1) begin
                            blk_inter_coded[i] <= 1'b0;
                            blk_ref0[i] <= REF_NONE;
                            blk_inter_mode[i] <= REDUCED_INTER_NONE;
                        end
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
                    frame_obu_start_addr <= bs_wr_addr;
                    bs_write_frm <= 1;
                    ec_init      <= 1;  // Initialize entropy coder
                    top_state    <= TS_WAIT_FRM;
                end
                TS_WAIT_FRM: begin
                    if (bs_done) begin
                        top_state <= TS_PART_PREP;
                    end
                end

                TS_PART_PREP: begin
                    if ((blk_x[2:0] == 3'd0) && (blk_y[2:0] == 3'd0))
                        part_stage <= 2'd3;
                    else if ((blk_x[1:0] == 2'd0) && (blk_y[1:0] == 2'd0))
                        part_stage <= 2'd2;
                    else if ((blk_x[0] == 1'b0) && (blk_y[0] == 1'b0))
                        part_stage <= 2'd1;
                    else
                        part_stage <= 2'd0;
                    top_state <= TS_PART_EMIT;
                end

                TS_PART_EMIT: begin
                    case (part_stage)
                        2'd3: begin
                            if ((({4'd0, blk_y, 3'b000} + 14'd32) < FRAME_HEIGHT) &&
                                (({4'd0, blk_x, 3'b000} + 14'd32) < FRAME_WIDTH)) begin
                                part_level_log2 <= 3'd6;
                                part_symbol     <= 5'd3; // PARTITION_SPLIT
                                part_nsyms      <= 5'd10;
                                ec_encode_symbol <= 1;
                                ec_symbol        <= 5'd3;
                                ec_nsyms         <= 5'd10;
                                ec_icdf_flat     <= partition_icdf_flat(
                                    get_partition_ctx_cur({4'd0, blk_x, 3'b000},
                                                          {4'd0, blk_y, 3'b000},
                                                          2'd3));
                                top_state <= TS_PART_WAIT;
                            end else begin
                                part_stage <= 2'd2;
                                top_state  <= TS_PART_EMIT;
                            end
                        end

                        2'd2: begin
                            if ((({4'd0, blk_y, 3'b000} + 14'd16) < FRAME_HEIGHT) &&
                                (({4'd0, blk_x, 3'b000} + 14'd16) < FRAME_WIDTH)) begin
                                part_level_log2 <= 3'd5;
                                part_symbol     <= 5'd3; // PARTITION_SPLIT
                                part_nsyms      <= 5'd10;
                                ec_encode_symbol <= 1;
                                ec_symbol        <= 5'd3;
                                ec_nsyms         <= 5'd10;
                                ec_icdf_flat     <= partition_icdf_flat(
                                    get_partition_ctx_cur({4'd0, blk_x, 3'b000},
                                                          {4'd0, blk_y, 3'b000},
                                                          2'd2));
                                top_state <= TS_PART_WAIT;
                            end else begin
                                part_stage <= 2'd1;
                                top_state  <= TS_PART_EMIT;
                            end
                        end

                        2'd1: begin
                            if ((({4'd0, blk_y, 3'b000} + 14'd8) < FRAME_HEIGHT) &&
                                (({4'd0, blk_x, 3'b000} + 14'd8) < FRAME_WIDTH)) begin
                                part_level_log2 <= 3'd4;
                                part_symbol     <= 5'd3; // PARTITION_SPLIT
                                part_nsyms      <= 5'd10;
                                ec_encode_symbol <= 1;
                                ec_symbol        <= 5'd3;
                                ec_nsyms         <= 5'd10;
                                ec_icdf_flat     <= partition_icdf_flat(
                                    get_partition_ctx_cur({4'd0, blk_x, 3'b000},
                                                          {4'd0, blk_y, 3'b000},
                                                          2'd1));
                                top_state <= TS_PART_WAIT;
                            end else begin
                                part_stage <= 2'd0;
                                top_state  <= TS_PART_EMIT;
                            end
                        end

                        default: begin
                            part_level_log2 <= 3'd3;
                            part_symbol     <= 5'd0; // PARTITION_NONE
                            part_nsyms      <= 5'd4;
                            ec_encode_symbol <= 1;
                            ec_symbol        <= 5'd0;
                            ec_nsyms         <= 5'd4;
                            ec_icdf_flat     <= partition_icdf_flat(
                                get_partition_ctx_cur({4'd0, blk_x, 3'b000},
                                                      {4'd0, blk_y, 3'b000},
                                                      2'd0));
                            top_state <= TS_PART_WAIT;
                        end
                    endcase
                end

                TS_PART_WAIT: begin
                    if (ec_done) begin
                        if (part_stage > 2'd0) begin
                            part_stage <= part_stage - 2'd1;
                            top_state  <= TS_PART_EMIT;
                        end else begin
                            top_state <= TS_FETCH_BLK;
                        end
                    end
                end

                // Fetch current 8x8 block
                TS_FETCH_BLK: begin
                    fetch_start     <= 1;
                    fetch_is_chroma <= 0;
                    fetch_chroma_id <= 0;
                    fetch_blk_x     <= blk_x;
                    fetch_blk_y     <= blk_y;
                    cur_block_has_coeff <= 1'b0;
                    cur_only_dc_nonzero <= 1'b1;
                    cur_only_reduced_ac_nonzero <= 1'b1;
                    cur_only_eob9_nonzero <= 1'b1;
                    cur_all_coeffs_le_14 <= 1'b1;
                    dc_br_remaining <= 5'd0;
                    coeff_br_remaining <= 5'd0;
                    coeff_br_capped <= 1'b0;
                    coeff_eob_bit_idx <= 4'd0;
                    golomb_zero_remaining <= 5'd0;
                    golomb_bit_idx <= 5'd0;
                    golomb_x <= 16'd0;
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
                        has_top_right <= (blk_y > 0) && (blk_x + 1 < BLK_COLS);
                        // For the current fixed 8x8/TX_8X8 raster path, bottom-left
                        // extension would require pixels from blocks that have not been
                        // reconstructed yet. Keep it unavailable until the traversal
                        // order and availability checks are made spec-accurate.
                        has_bottom_left <= 1'b0;
                        // Default neighbor values (overwritten by loader)
                        for (i = 0; i < 8; i = i + 1) begin
                            top_pixels[i]  <= 8'd128;
                            top_right_pixels[i] <= 8'd128;
                            left_pixels[i] <= 8'd128;
                            bottom_left_pixels[i] <= 8'd128;
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
                                top_state <= TS_PREDICT_INIT;
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
                    end else if (neigh_cnt <= 6'd8) begin
                        // Top pixels [0..7]
                        if (has_top) begin
                            neigh_rd_addr <= (blk_y * 8 - 1) * FRAME_WIDTH + blk_x * 8 + (neigh_cnt - 6'd1);
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            neigh_cnt <= 6'd9;  // skip top, try left
                        end
                    end else if (neigh_cnt <= 6'd16) begin
                        // Left pixels [0..7]
                        if (has_left) begin
                            neigh_rd_addr <= (blk_y * 8 + (neigh_cnt - 6'd9)) * FRAME_WIDTH + blk_x * 8 - 1;
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            neigh_cnt <= 6'd17;  // skip left, try top-right
                        end
                    end else if (neigh_cnt <= 6'd24) begin
                        // Top-right pixels [0..7]
                        if (has_top_right) begin
                            neigh_rd_addr <= (blk_y * 8 - 1) * FRAME_WIDTH + blk_x * 8 + 8 + (neigh_cnt - 6'd17);
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            neigh_cnt <= 6'd25;  // skip top-right, try bottom-left
                        end
                    end else if (neigh_cnt <= 6'd32) begin
                        // Bottom-left pixels [0..7]
                        if (has_bottom_left) begin
                            neigh_rd_addr <= (blk_y * 8 + 8 + (neigh_cnt - 6'd25)) * FRAME_WIDTH + blk_x * 8 - 1;
                            top_state <= TS_NEIGH_READ;
                        end else begin
                            neigh_rd_active <= 0;
                            if (!is_keyframe)
                                top_state <= TS_ME_START;
                            else begin
                                use_inter <= 0;
                                top_state <= TS_PREDICT_INIT;
                            end
                        end
                    end
                end

                TS_NEIGH_READ: begin
                    // Store the pixel read from reference memory
                    if (neigh_cnt == 5'd0)
                        top_left_pixel <= ref_mem_rd_data;
                    else if (neigh_cnt <= 6'd8)
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
                    else if (neigh_cnt <= 6'd16)
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
                    else if (neigh_cnt <= 6'd24)
                        case (neigh_cnt)
                            6'd17: top_right_pixels[0] <= ref_mem_rd_data;
                            6'd18: top_right_pixels[1] <= ref_mem_rd_data;
                            6'd19: top_right_pixels[2] <= ref_mem_rd_data;
                            6'd20: top_right_pixels[3] <= ref_mem_rd_data;
                            6'd21: top_right_pixels[4] <= ref_mem_rd_data;
                            6'd22: top_right_pixels[5] <= ref_mem_rd_data;
                            6'd23: top_right_pixels[6] <= ref_mem_rd_data;
                            6'd24: top_right_pixels[7] <= ref_mem_rd_data;
                            default: ;
                        endcase
                    else
                        case (neigh_cnt)
                            6'd25: bottom_left_pixels[0] <= ref_mem_rd_data;
                            6'd26: bottom_left_pixels[1] <= ref_mem_rd_data;
                            6'd27: bottom_left_pixels[2] <= ref_mem_rd_data;
                            6'd28: bottom_left_pixels[3] <= ref_mem_rd_data;
                            6'd29: bottom_left_pixels[4] <= ref_mem_rd_data;
                            6'd30: bottom_left_pixels[5] <= ref_mem_rd_data;
                            6'd31: bottom_left_pixels[6] <= ref_mem_rd_data;
                            6'd32: bottom_left_pixels[7] <= ref_mem_rd_data;
                            default: ;
                        endcase

                    if (neigh_cnt >= 6'd32) begin
                        // All neighbors loaded
                        neigh_rd_active <= 0;
                        if (!is_keyframe && !force_intra_in)
                            top_state <= TS_ME_START;
                        else begin
                            use_inter <= 0;
                            top_state <= TS_PREDICT_INIT;
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
                        use_inter <= force_intra_in ? 1'b0 :
                                     ((me_best_sad < INTRA_SAD_THRESHOLD) &&
                                      (me_best_mvx == 9'sd0) &&
                                      (me_best_mvy == 9'sd0));
                        top_state <= TS_PREDICT_INIT;
                    end
                end

                // Select between inter prediction for P-blocks and intra mode
                // search for keyframes / intra-coded blocks.
                TS_PREDICT_INIT: begin
                    if (use_inter && !is_keyframe) begin
                        inter_base_x <= ($signed({1'b0, blk_x}) <<< 3) + me_mvx;
                        inter_base_y <= ($signed({1'b0, blk_y}) <<< 3) + me_mvy;
                        inter_fetch_idx <= 6'd0;
                        best_intra_mode <= AV1_DC_PRED;
                        top_state <= TS_INTER_ADDR;
                    end else begin
                        intra_eval_idx <= 4'd0;
                        intra_best_sad <= 18'h3FFFF;
                        best_intra_mode <= AV1_DC_PRED;
                        top_state <= TS_PREDICT;
                    end
                end

                // Integer-pel inter predictor fetch from the reference frame.
                TS_INTER_ADDR: begin
                    inter_rd_active <= 1;
                    inter_rd_addr <= (inter_base_y + $signed({1'b0, inter_fetch_idx[5:3]})) * FRAME_WIDTH +
                                     (inter_base_x + $signed({1'b0, inter_fetch_idx[2:0]}));
                    top_state <= TS_INTER_READ;
                end

                TS_INTER_READ: begin
                    pred_blk[inter_fetch_idx] <= ref_mem_rd_data;
                    residual[inter_fetch_idx] <= $signed({1'b0, cur_blk[inter_fetch_idx]}) -
                                                $signed({1'b0, ref_mem_rd_data});

                    if (inter_fetch_idx < 6'd63) begin
                        inter_fetch_idx <= inter_fetch_idx + 1'b1;
                        top_state <= TS_INTER_ADDR;
                    end else begin
                        inter_rd_active <= 0;
                        xform_row <= 0;
                        top_state <= TS_XFORM_ROW;
                    end
                end

                // Prediction
                TS_PREDICT: begin
                    pred_start <= 1;
                    top_state  <= TS_WAIT_PRED;
                end
                TS_WAIT_PRED: begin
                    if (pred_done) begin
                        intra_cand_sad = 18'd0;
                        for (i = 0; i < 64; i = i + 1) begin
                            if (cur_blk[i] > pred_out[i])
                                intra_cand_sad = intra_cand_sad + (cur_blk[i] - pred_out[i]);
                            else
                                intra_cand_sad = intra_cand_sad + (pred_out[i] - cur_blk[i]);
                        end

                        if (intra_eval_idx < 4'd10) begin
                            if (intra_cand_sad < intra_best_sad) begin
                                intra_best_sad <= intra_cand_sad;
                                best_intra_mode <= intra_eval_mode;
                                for (i = 0; i < 64; i = i + 1)
                                    pred_blk[i] <= pred_out[i];
                            end

                            intra_eval_idx <= intra_eval_idx + 1'b1;
                            top_state <= TS_PREDICT;
                        end else begin
                            if (intra_cand_sad < intra_best_sad) begin
                                intra_best_sad <= intra_cand_sad;
                                best_intra_mode <= intra_eval_mode;
                                for (i = 0; i < 64; i = i + 1) begin
                                    pred_blk[i] <= pred_out[i];
                                    residual[i] <= $signed({1'b0, cur_blk[i]}) -
                                                   $signed({1'b0, pred_out[i]});
                                end
                            end else begin
                                for (i = 0; i < 64; i = i + 1) begin
                                    residual[i] <= $signed({1'b0, cur_blk[i]}) -
                                                   $signed({1'b0, pred_blk[i]});
                                end
                            end

                            xform_row <= 0;
                            top_state <= TS_XFORM_ROW;
                        end
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
                        qcoeff[proc_idx] <= (dc_only_in && proc_idx != 0) ? 16'sd0 : quant_coeff_out;
                        if (((dc_only_in && proc_idx != 0) ? 16'sd0 : quant_coeff_out) != 16'sd0) begin
                            cur_block_has_coeff <= 1'b1;
                            if (proc_idx != 0)
                                cur_only_dc_nonzero <= 1'b0;
                            if (proc_idx != 0 && proc_idx != 1 && proc_idx != 8)
                                cur_only_reduced_ac_nonzero <= 1'b0;
                            if (proc_idx != 0 && proc_idx != 1 && proc_idx != 8 && proc_idx != 10)
                                cur_only_eob9_nonzero <= 1'b0;
                            if (abs16((dc_only_in && proc_idx != 0) ? 16'sd0 : quant_coeff_out) > 16'd14)
                                cur_all_coeffs_le_14 <= 1'b0;
                        end
                        if (proc_idx == 0)
                            dequant_dc <= quant_dequant_out;
                        else if (proc_idx == 1)
                            dequant_ac <= quant_dequant_out;

                        if (proc_idx < 63) begin
                            proc_idx  <= proc_idx + 1;
                            top_state <= TS_QCOEFF_START;
                        end else begin
                            // All 64 coefficients are available. Begin the
                            // block-level syntax pass from the RTL-owned skip
                            // symbol before falling through the older
                            // coefficient placeholder stream.
                            proc_idx  <= 0;
                            top_state <= TS_SYNTAX_SKIP;
                        end
                    end
                end

                TS_SYNTAX_SKIP: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= cur_block_skip ? 5'd1 : 5'd0;
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= skip_icdf_flat(cur_skip_ctx);
                    top_state        <= TS_SYNTAX_WAIT;
                end

                TS_SYNTAX_WAIT: begin
                    if (ec_done) begin
                        if (!is_keyframe) begin
                            top_state <= TS_SYNTAX_II;
                        end else if (!use_inter) begin
                            top_state <= TS_SYNTAX_YMODE;
                        end else if (cur_block_skip) begin
                            proc_idx  <= 0;
                            top_state <= TS_IQ_START;
                        end else begin
                            proc_idx  <= 0;
                            top_state <= TS_TXB_SKIP_Y;
                        end
                    end
                end

                TS_SYNTAX_II: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= use_inter ? 5'd1 : 5'd0;
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= intra_inter_icdf_flat(cur_intra_inter_ctx);
                    top_state        <= TS_SYNTAX_IIWAIT;
                end

                TS_SYNTAX_IIWAIT: begin
                    if (ec_done) begin
                        if (!use_inter) begin
                            top_state <= TS_SYNTAX_YMODE;
                        end else begin
                            top_state <= TS_SYNTAX_REF1;
                        end
                    end
                end

                TS_SYNTAX_REF1: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // choose forward refs
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= single_ref_icdf_flat(
                        compare_ref_counts_fn(
                            ((inter_above[blk_x << 1] && ref_is_forward_fn(ref_above[blk_x << 1])) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_is_forward_fn(ref_left[blk_y << 1])) ? 1 : 0),
                            ((inter_above[blk_x << 1] && ref_is_backward_fn(ref_above[blk_x << 1])) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_is_backward_fn(ref_left[blk_y << 1])) ? 1 : 0)
                        ),
                        2'd0);
                    top_state        <= TS_SYNTAX_REF1W;
                end

                TS_SYNTAX_REF1W: begin
                    if (ec_done)
                        top_state <= TS_SYNTAX_REF2;
                end

                TS_SYNTAX_REF2: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // choose LAST/LAST2 group
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= single_ref_icdf_flat(
                        compare_ref_counts_fn(
                            ((inter_above[blk_x << 1] && ref_above[blk_x << 1] == REF_LAST) ? 1 : 0) +
                            ((inter_above[blk_x << 1] && ref_above[blk_x << 1] == REF_LAST2) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_left[blk_y << 1] == REF_LAST) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_left[blk_y << 1] == REF_LAST2) ? 1 : 0),
                            ((inter_above[blk_x << 1] && ref_above[blk_x << 1] == REF_LAST3) ? 1 : 0) +
                            ((inter_above[blk_x << 1] && ref_above[blk_x << 1] == REF_GOLDEN) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_left[blk_y << 1] == REF_LAST3) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_left[blk_y << 1] == REF_GOLDEN) ? 1 : 0)
                        ),
                        2'd1);
                    top_state        <= TS_SYNTAX_REF2W;
                end

                TS_SYNTAX_REF2W: begin
                    if (ec_done)
                        top_state <= TS_SYNTAX_REF3;
                end

                TS_SYNTAX_REF3: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // choose LAST
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= single_ref_icdf_flat(
                        compare_ref_counts_fn(
                            ((inter_above[blk_x << 1] && ref_above[blk_x << 1] == REF_LAST) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_left[blk_y << 1] == REF_LAST) ? 1 : 0),
                            ((inter_above[blk_x << 1] && ref_above[blk_x << 1] == REF_LAST2) ? 1 : 0) +
                            ((inter_left[blk_y << 1] && ref_left[blk_y << 1] == REF_LAST2) ? 1 : 0)
                        ),
                        2'd2);
                    top_state        <= TS_SYNTAX_REF3W;
                end

                TS_SYNTAX_REF3W: begin
                    if (ec_done)
                        top_state <= TS_SYNTAX_NEWMV;
                end

                TS_SYNTAX_NEWMV: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // GLOBALMV is not NEWMV
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= newmv_icdf_flat(
                        get_reduced_single_ref_mode_ctx_fn(blk_x, blk_y, REF_LAST)[2:0]);
                    top_state        <= TS_SYNTAX_NEWMVW;
                end

                TS_SYNTAX_NEWMVW: begin
                    if (ec_done)
                        top_state <= TS_SYNTAX_ZEROMV;
                end

                TS_SYNTAX_ZEROMV: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // GLOBALMV
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= zeromv_icdf_flat(
                        get_reduced_single_ref_mode_ctx_fn(blk_x, blk_y, REF_LAST)[AV1_GLOBALMV_OFFSET]);
                    top_state        <= TS_SYNTAX_ZEROMVW;
                end

                TS_SYNTAX_ZEROMVW: begin
                    if (ec_done) begin
                        if (cur_block_skip) begin
                            proc_idx  <= 0;
                            top_state <= TS_IQ_START;
                        end else begin
                            proc_idx  <= 0;
                            top_state <= TS_TXB_SKIP_Y;
                        end
                    end
                end

                TS_SYNTAX_YMODE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= {1'b0, best_intra_mode};
                    ec_nsyms         <= 5'd13;
                    ec_icdf_flat     <= is_keyframe ? cur_kf_y_icdf : cur_if_y_icdf;
                    top_state        <= TS_SYNTAX_YWAIT;
                end

                TS_SYNTAX_YWAIT: begin
                    if (ec_done) begin
                        if (is_directional_mode(best_intra_mode))
                            top_state <= TS_SYNTAX_ANGLE;
                        else
                            top_state <= TS_SYNTAX_UVMODE;
                    end
                end

                TS_SYNTAX_ANGLE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd3; // angle_delta = 0
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_ang_icdf;
                    top_state        <= TS_SYNTAX_AWAIT;
                end

                TS_SYNTAX_AWAIT: begin
                    if (ec_done)
                        top_state <= TS_SYNTAX_UVMODE;
                end

                TS_SYNTAX_UVMODE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // UV_DC_PRED
                    ec_nsyms         <= 5'd14;
                    ec_icdf_flat     <= cur_uv_icdf;
                    top_state        <= TS_SYNTAX_UVWAIT;
                end

                TS_SYNTAX_UVWAIT: begin
                    if (ec_done) begin
                        if (cur_block_skip) begin
                            proc_idx  <= 0;
                            top_state <= TS_IQ_START;
                        end else begin
                            proc_idx  <= 0;
                            top_state <= TS_TXB_SKIP_Y;
                        end
                    end
                end

                TS_TXB_SKIP_Y: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // luma txb_skip = 0, coefficients present
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_txb_luma_icdf;
                    top_state        <= TS_TXB_SKIP_YW;
                end

                TS_TXB_SKIP_YW: begin
                    if (ec_done) begin
                        if (!use_inter && cur_scan01_coeff_path)
                            top_state <= TS_AC01_TX_TYPE;
                        else if (!use_inter && cur_eob9_coeff_path)
                            top_state <= TS_AC09_TX_TYPE;
                        else if (!use_inter && cur_dc_only_coeff_path)
                            top_state <= TS_DC_TX_TYPE;
                        else if (cur_generic_coeff_path)
                            top_state <= TS_GEN_TX_TYPE;
                        else begin
                            proc_idx  <= 0;
                            top_state <= TS_COEFF_SYM;
                        end
                    end
                end

                TS_DC_TX_TYPE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // DCT_DCT intra tx_type
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_intra_tx_icdf;
                    top_state        <= TS_DC_TX_WAIT;
                end

                TS_DC_TX_WAIT: begin
                    if (ec_done)
                        top_state <= TS_DC_EOB;
                end

                TS_DC_EOB: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // eob_pt - 1 for eob=1
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_eob_multi_icdf;
                    top_state        <= TS_DC_EOB_WAIT;
                end

                TS_DC_EOB_WAIT: begin
                    if (ec_done)
                        top_state <= TS_DC_BASE;
                end

                TS_DC_BASE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= coeff_base_eob_sym_from_level(abs16(qcoeff[0]));
                    ec_nsyms         <= 5'd3;
                    ec_icdf_flat     <= cur_base_eob_icdf;
                    top_state        <= TS_DC_BASE_WAIT;
                end

                TS_DC_BASE_WAIT: begin
                    if (ec_done) begin
                        if (abs16(qcoeff[0]) > 16'd2) begin
                            dc_br_remaining <= abs16(qcoeff[0]) - 16'd3;
                            top_state <= TS_DC_BR;
                        end else begin
                            top_state <= TS_DC_SIGN;
                        end
                    end
                end

                TS_DC_BR: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= (dc_br_remaining > 5'd3) ? 5'd3 : dc_br_remaining;
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_coeff_br_icdf;
                    top_state        <= TS_DC_BR_WAIT;
                end

                TS_DC_BR_WAIT: begin
                    if (ec_done) begin
                        if (dc_br_remaining >= 5'd3) begin
                            dc_br_remaining <= dc_br_remaining - 5'd3;
                            top_state <= TS_DC_BR;
                        end else begin
                            dc_br_remaining <= 5'd0;
                            top_state <= TS_DC_SIGN;
                        end
                    end
                end

                TS_DC_SIGN: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= qcoeff[0][15] ? 5'd1 : 5'd0;
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_dc_sign_icdf;
                    top_state        <= TS_DC_SIGN_WAIT;
                end

                TS_DC_SIGN_WAIT: begin
                    if (ec_done)
                        top_state <= TS_TXB_SKIP_CB;
                end

                TS_AC01_TX_TYPE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // DCT_DCT intra tx_type
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_intra_tx_icdf;
                    top_state        <= TS_AC01_TX_WAIT;
                end

                TS_AC01_TX_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC01_EOB;
                end

                TS_AC01_EOB: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd2; // eob_pt - 1 for eob=3
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_eob_multi_icdf;
                    top_state        <= TS_AC01_EOB_WAIT;
                end

                TS_AC01_EOB_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC01_EOB_EXTRA;
                end

                TS_AC01_EOB_EXTRA: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // eob_extra for eob=3
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_eob_extra_icdf;
                    top_state        <= TS_AC01_EOB_EXWAIT;
                end

                TS_AC01_EOB_EXWAIT: begin
                    if (ec_done)
                        top_state <= TS_AC01_AC_BASE;
                end

                TS_AC01_AC_BASE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // |qcoeff[1]| == 1 -> level-1 = 0
                    ec_nsyms         <= 5'd3;
                    ec_icdf_flat     <= cur_base_eob1_icdf;
                    top_state        <= TS_AC01_AC_WAIT;
                end

                TS_AC01_AC_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC01_SCAN1_BASE;
                end

                TS_AC01_SCAN1_BASE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // scan position 1 (pos=8) is zero
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base1_icdf;
                    top_state        <= TS_AC01_SCAN1_WAIT;
                end

                TS_AC01_SCAN1_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC01_DC_BASE;
                end

                TS_AC01_DC_BASE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= {3'd0, abs16(qcoeff[0])[1:0]}; // non-EOB base symbol
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base0_icdf;
                    top_state        <= TS_AC01_DC_WAIT;
                end

                TS_AC01_DC_WAIT: begin
                    if (ec_done) begin
                        if (abs16(qcoeff[0]) > 16'd2) begin
                            dc_br_remaining <= abs16(qcoeff[0]) - 16'd3;
                            top_state <= TS_AC01_BR;
                        end else begin
                            top_state <= TS_AC01_SIGN_DC;
                        end
                    end
                end

                TS_AC01_BR: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= (dc_br_remaining > 5'd3) ? 5'd3 : dc_br_remaining;
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_coeff_br1_icdf;
                    top_state        <= TS_AC01_BR_WAIT;
                end

                TS_AC01_BR_WAIT: begin
                    if (ec_done) begin
                        if (dc_br_remaining >= 5'd3) begin
                            dc_br_remaining <= dc_br_remaining - 5'd3;
                            top_state <= TS_AC01_BR;
                        end else begin
                            dc_br_remaining <= 5'd0;
                            top_state <= TS_AC01_SIGN_DC;
                        end
                    end
                end

                TS_AC01_SIGN_DC: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= qcoeff[0][15] ? 5'd1 : 5'd0;
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_dc_sign_icdf;
                    top_state        <= TS_AC01_SIGN_DCW;
                end

                TS_AC01_SIGN_DCW: begin
                    if (ec_done)
                        top_state <= TS_AC01_SIGN_AC;
                end

                TS_AC01_SIGN_AC: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= qcoeff[1][15];
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_AC01_SIGN_ACW;
                end

                TS_AC01_SIGN_ACW: begin
                    if (ec_done)
                        top_state <= TS_TXB_SKIP_CB;
                end

                TS_AC09_TX_TYPE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // DCT_DCT intra tx_type
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_intra_tx_icdf;
                    top_state        <= TS_AC09_TX_WAIT;
                end

                TS_AC09_TX_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC09_EOB;
                end

                TS_AC09_EOB: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd4; // eob_pt - 1 for eob=9
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_eob_multi_icdf;
                    top_state        <= TS_AC09_EOB_WAIT;
                end

                TS_AC09_EOB_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC09_EOB_EXTRA;
                end

                TS_AC09_EOB_EXTRA: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // first eob_extra bit for eob=9
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_eob_extra2_icdf;
                    top_state        <= TS_AC09_EOB_EXWAIT;
                end

                TS_AC09_EOB_EXWAIT: begin
                    if (ec_done)
                        top_state <= TS_AC09_EOB_BIT1;
                end

                TS_AC09_EOB_BIT1: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= 1'b0;
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_AC09_EOB_BIT1W;
                end

                TS_AC09_EOB_BIT1W: begin
                    if (ec_done)
                        top_state <= TS_AC09_EOB_BIT0;
                end

                TS_AC09_EOB_BIT0: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= 1'b0;
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_AC09_EOB_BIT0W;
                end

                TS_AC09_EOB_BIT0W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE10;
                end

                TS_AC09_BASE10: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // |qcoeff[10]| == 1 -> level-1 = 0
                    ec_nsyms         <= 5'd3;
                    ec_icdf_flat     <= cur_base_eob1_icdf;
                    top_state        <= TS_AC09_BASE10W;
                end

                TS_AC09_BASE10W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE17;
                end

                TS_AC09_BASE17: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // scan position 7 (pos=17) is zero
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base6_icdf;
                    top_state        <= TS_AC09_BASE17W;
                end

                TS_AC09_BASE17W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE24;
                end

                TS_AC09_BASE24: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // scan position 6 (pos=24) is zero
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base6_icdf;
                    top_state        <= TS_AC09_BASE24W;
                end

                TS_AC09_BASE24W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE16;
                end

                TS_AC09_BASE16: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // scan position 5 (pos=16) is zero
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base6_icdf;
                    top_state        <= TS_AC09_BASE16W;
                end

                TS_AC09_BASE16W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE9;
                end

                TS_AC09_BASE9: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // scan position 4 (pos=9) is zero
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base7_icdf;
                    top_state        <= TS_AC09_BASE9W;
                end

                TS_AC09_BASE9W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE2;
                end

                TS_AC09_BASE2: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd0; // scan position 3 (pos=2) is zero
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base7_icdf;
                    top_state        <= TS_AC09_BASE2W;
                end

                TS_AC09_BASE2W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE1;
                end

                TS_AC09_BASE1: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd2; // |qcoeff[1]| == 2
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base2_icdf;
                    top_state        <= TS_AC09_BASE1W;
                end

                TS_AC09_BASE1W: begin
                    if (ec_done)
                        top_state <= TS_AC09_BASE8;
                end

                TS_AC09_BASE8: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // |qcoeff[8]| == 1
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base2_icdf;
                    top_state        <= TS_AC09_BASE8W;
                end

                TS_AC09_BASE8W: begin
                    if (ec_done)
                        top_state <= TS_AC09_DC_BASE;
                end

                TS_AC09_DC_BASE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // |qcoeff[0]| == 1
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= cur_base0_icdf;
                    top_state        <= TS_AC09_DC_WAIT;
                end

                TS_AC09_DC_WAIT: begin
                    if (ec_done)
                        top_state <= TS_AC09_SIGN_DC;
                end

                TS_AC09_SIGN_DC: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= qcoeff[0][15] ? 5'd1 : 5'd0;
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_dc_sign_icdf;
                    top_state        <= TS_AC09_SIGN_DCW;
                end

                TS_AC09_SIGN_DCW: begin
                    if (ec_done)
                        top_state <= TS_AC09_SIGN_AC8;
                end

                TS_AC09_SIGN_AC8: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= qcoeff[8][15];
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_AC09_SIGN_AC8W;
                end

                TS_AC09_SIGN_AC8W: begin
                    if (ec_done)
                        top_state <= TS_AC09_SIGN_AC1;
                end

                TS_AC09_SIGN_AC1: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= qcoeff[1][15];
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_AC09_SIGN_AC1W;
                end

                TS_AC09_SIGN_AC1W: begin
                    if (ec_done)
                        top_state <= TS_AC09_SIGN_AC10;
                end

                TS_AC09_SIGN_AC10: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= qcoeff[10][15];
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_AC09_SIGN_AC10W;
                end

                TS_AC09_SIGN_AC10W: begin
                    if (ec_done)
                        top_state <= TS_TXB_SKIP_CB;
                end

                TS_GEN_TX_TYPE: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // DCT_DCT intra tx_type
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_intra_tx_icdf;
                    top_state        <= TS_GEN_TX_WAIT;
                end

                TS_GEN_TX_WAIT: begin
                    if (ec_done)
                        top_state <= TS_GEN_EOB;
                end

                TS_GEN_EOB: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= cur_generic_eob_pt - 4'd1;
                    ec_nsyms         <= 5'd7;
                    ec_icdf_flat     <= cur_eob_multi_icdf;
                    top_state        <= TS_GEN_EOB_WAIT;
                end

                TS_GEN_EOB_WAIT: begin
                    if (ec_done) begin
                        if (cur_generic_eob_bits != 4'd0)
                            top_state <= TS_GEN_EOB_EXTRA;
                        else begin
                            proc_idx  <= cur_generic_eob[5:0] - 6'd1;
                            top_state <= TS_GEN_BASE;
                        end
                    end
                end

                TS_GEN_EOB_EXTRA: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= (cur_generic_eob_extra >> (cur_generic_eob_bits - 4'd1)) & 10'd1;
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= eob_extra_ctx_icdf_flat_qctx(cur_coeff_qctx, 1'b0, cur_generic_eob_pt - 4'd3);
                    top_state        <= TS_GEN_EOB_EXWAIT;
                end

                TS_GEN_EOB_EXWAIT: begin
                    if (ec_done) begin
                        if (cur_generic_eob_bits > 4'd1) begin
                            coeff_eob_bit_idx <= cur_generic_eob_bits - 4'd2;
                            top_state <= TS_GEN_EOB_BIT;
                        end else begin
                            proc_idx  <= cur_generic_eob[5:0] - 6'd1;
                            top_state <= TS_GEN_BASE;
                        end
                    end
                end

                TS_GEN_EOB_BIT: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= (cur_generic_eob_extra >> coeff_eob_bit_idx) & 10'd1;
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_GEN_EOB_BITW;
                end

                TS_GEN_EOB_BITW: begin
                    if (ec_done) begin
                        if (coeff_eob_bit_idx > 4'd0) begin
                            coeff_eob_bit_idx <= coeff_eob_bit_idx - 4'd1;
                            top_state <= TS_GEN_EOB_BIT;
                        end else begin
                            proc_idx  <= cur_generic_eob[5:0] - 6'd1;
                            top_state <= TS_GEN_BASE;
                        end
                    end
                end

                TS_GEN_BASE: begin
                    ec_encode_symbol <= 1;
                    if (proc_idx == (cur_generic_eob[5:0] - 6'd1)) begin
                        ec_symbol    <= (coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) > 16'd3) ?
                                        5'd2 : ({1'b0, coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx))[3:0]} - 5'd1);
                        ec_nsyms     <= 5'd3;
                        ec_icdf_flat <= coeff_base_eob_ctx_icdf_flat_qctx(cur_coeff_qctx, coeff_base_eob_ctx_from_scan_fn({1'b0, proc_idx}));
                    end else begin
                        ec_symbol    <= (coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) > 16'd3) ?
                                        5'd3 : {1'b0, coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx))[3:0]};
                        ec_nsyms     <= 5'd4;
                        ec_icdf_flat <= coeff_base_ctx_icdf_flat_qctx(cur_coeff_qctx, get_nz_map_ctx_qcoeff(scan_8x8_pos(proc_idx)));
                    end
                    top_state <= TS_GEN_BASEW;
                end

                TS_GEN_BASEW: begin
                    if (ec_done) begin
                        if (coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) > 16'd2) begin
                            if (coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) > 16'd14) begin
                                coeff_br_remaining <= 5'd12;
                                coeff_br_capped    <= 1'b1;
                            end else begin
                                coeff_br_remaining <= coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx))[4:0] - 5'd3;
                                coeff_br_capped    <= 1'b0;
                            end
                            top_state <= TS_GEN_BR;
                        end else if (proc_idx > 6'd0) begin
                            coeff_br_capped <= 1'b0;
                            proc_idx  <= proc_idx - 6'd1;
                            top_state <= TS_GEN_BASE;
                        end else begin
                            coeff_br_capped <= 1'b0;
                            proc_idx  <= 6'd0;
                            top_state <= TS_GEN_SIGN;
                        end
                    end
                end

                TS_GEN_BR: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= (coeff_br_remaining > 5'd3) ? 5'd3 : coeff_br_remaining;
                    ec_nsyms         <= 5'd4;
                    ec_icdf_flat     <= coeff_br_ctx_icdf_flat_qctx(cur_coeff_qctx, get_br_ctx_qcoeff(scan_8x8_pos(proc_idx)));
                    top_state        <= TS_GEN_BRW;
                end

                TS_GEN_BRW: begin
                    if (ec_done) begin
                        if (coeff_br_remaining >= 5'd3) begin
                            if ((coeff_br_remaining == 5'd3) && coeff_br_capped) begin
                                coeff_br_capped <= 1'b0;
                                top_state <= (proc_idx > 6'd0) ? TS_GEN_BASE : TS_GEN_SIGN;
                                if (proc_idx > 6'd0)
                                    proc_idx <= proc_idx - 6'd1;
                                else
                                    proc_idx <= 6'd0;
                            end else begin
                                coeff_br_remaining <= coeff_br_remaining - 5'd3;
                                top_state <= TS_GEN_BR;
                            end
                        end else if (proc_idx > 6'd0) begin
                            coeff_br_capped <= 1'b0;
                            proc_idx  <= proc_idx - 6'd1;
                            top_state <= TS_GEN_BASE;
                        end else begin
                            coeff_br_capped <= 1'b0;
                            proc_idx  <= 6'd0;
                            top_state <= TS_GEN_SIGN;
                        end
                    end
                end

                TS_GEN_SIGN: begin
                    if (qcoeff[scan_8x8_pos(proc_idx)] != 16'sd0) begin
                        if (proc_idx == 6'd0) begin
                            ec_encode_symbol <= 1;
                            ec_symbol        <= qcoeff[scan_8x8_pos(proc_idx)][15] ? 5'd1 : 5'd0;
                            ec_nsyms         <= 5'd2;
                            ec_icdf_flat     <= cur_dc_sign_icdf;
                        end else begin
                            ec_encode_bool <= 1;
                            ec_bool_val    <= qcoeff[scan_8x8_pos(proc_idx)][15];
                            ec_bool_prob   <= 15'd16384;
                        end
                        top_state <= TS_GEN_SIGNW;
                    end else if (proc_idx < (cur_generic_eob[5:0] - 6'd1)) begin
                        proc_idx  <= proc_idx + 6'd1;
                        top_state <= TS_GEN_SIGN;
                    end else begin
                        top_state <= TS_TXB_SKIP_CB;
                    end
                end

                TS_GEN_SIGNW: begin
                    if (ec_done) begin
                        if (coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) > 16'd14) begin
                            golomb_x <= coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) - 16'd14;
                            golomb_zero_remaining <= bit_length16_fn(
                                coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) - 16'd14) - 5'd1;
                            golomb_bit_idx <= bit_length16_fn(
                                coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) - 16'd14) - 5'd1;
                            if (bit_length16_fn(coeff_abs_level_qcoeff_flat(scan_8x8_pos(proc_idx)) - 16'd14) > 5'd1)
                                top_state <= TS_GEN_GOLOMB_ZERO;
                            else
                                top_state <= TS_GEN_GOLOMB_BIT;
                        end else if (proc_idx < (cur_generic_eob[5:0] - 6'd1)) begin
                            proc_idx  <= proc_idx + 6'd1;
                            top_state <= TS_GEN_SIGN;
                        end else begin
                            top_state <= TS_TXB_SKIP_CB;
                        end
                    end
                end

                TS_GEN_GOLOMB_ZERO: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= 1'b0;
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_GEN_GOLOMB_ZW;
                end

                TS_GEN_GOLOMB_ZW: begin
                    if (ec_done) begin
                        if (golomb_zero_remaining > 5'd1) begin
                            golomb_zero_remaining <= golomb_zero_remaining - 5'd1;
                            top_state <= TS_GEN_GOLOMB_ZERO;
                        end else begin
                            top_state <= TS_GEN_GOLOMB_BIT;
                        end
                    end
                end

                TS_GEN_GOLOMB_BIT: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= (golomb_x >> golomb_bit_idx) & 16'd1;
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_GEN_GOLOMB_BW;
                end

                TS_GEN_GOLOMB_BW: begin
                    if (ec_done) begin
                        if (golomb_bit_idx > 5'd0) begin
                            golomb_bit_idx <= golomb_bit_idx - 5'd1;
                            top_state <= TS_GEN_GOLOMB_BIT;
                        end else if (proc_idx < (cur_generic_eob[5:0] - 6'd1)) begin
                            proc_idx  <= proc_idx + 6'd1;
                            top_state <= TS_GEN_SIGN;
                        end else begin
                            top_state <= TS_TXB_SKIP_CB;
                        end
                    end
                end

                TS_COEFF_SYM: begin
                    ec_encode_bool <= 1;
                    ec_bool_val    <= (qcoeff[proc_idx] != 16'sd0);
                    ec_bool_prob   <= 15'd16384;
                    top_state      <= TS_COEFF_WAIT;
                end

                TS_COEFF_WAIT: begin
                    if (ec_done) begin
                        if (proc_idx < 63) begin
                            proc_idx  <= proc_idx + 1'b1;
                            top_state <= TS_COEFF_SYM;
                        end else begin
                            proc_idx  <= 0;
                            top_state <= TS_TXB_SKIP_CB;
                        end
                    end
                end

                TS_TXB_SKIP_CB: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // Cb txb_skip = 1, all zero
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_txb_chr_icdf;
                    top_state        <= TS_TXB_SKIP_CBW;
                end

                TS_TXB_SKIP_CBW: begin
                    if (ec_done)
                        top_state <= TS_TXB_SKIP_CR;
                end

                TS_TXB_SKIP_CR: begin
                    ec_encode_symbol <= 1;
                    ec_symbol        <= 5'd1; // Cr txb_skip = 1, all zero
                    ec_nsyms         <= 5'd2;
                    ec_icdf_flat     <= cur_txb_chr_icdf;
                    top_state        <= TS_TXB_SKIP_CRW;
                end

                TS_TXB_SKIP_CRW: begin
                    if (ec_done) begin
                        proc_idx  <= 0;
                        top_state <= TS_IQ_START;
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
                        // The captured qcoeff[] plane uses the repo's local row-major
                        // transpose of libaom's coefficient indexing. Transpose back
                        // into the mathematical 8x8 coefficient matrix before the 2D
                        // inverse transform so RTL reconstruction matches decoder output.
                        residual[{proc_idx[2:0], proc_idx[5:3]}] <= iq_dqcoeff_out;
                        if (proc_idx < 63) begin
                            proc_idx  <= proc_idx + 1;
                            top_state <= TS_IQ_START;
                        end else begin
                            // Do inverse row transforms first (2D IDCT)
                            xform_row <= 0;
                            top_state <= TS_IXFORM;
                        end
                    end
                end

                // Inverse column transform (2D IDCT step 2)
                TS_IXFORM_COL: begin
                    for (i = 0; i < 8; i = i + 1)
                        ixform_in[i] <= residual[i * 8 + xform_col];
                    ixform_start <= 1;
                    top_state    <= TS_IXFORM_COL_WT;
                end

                TS_IXFORM_COL_WT: begin
                    if (ixform_done) begin
                        // Reconstruct: recon = clamp(pred + round_shift(inv_residual, 4), 0, 255)
                        // TX_8X8 inverse DCT uses AV1's {-1, -4} shift schedule:
                        // a rounded /2 after the first pass, then a rounded /16 after
                        // the second pass. The first-pass rounding is applied in
                        // TS_RECON, so the final column output only needs /16 here.
                        for (i = 0; i < 8; i = i + 1) begin
                            begin
                                reg signed [16:0] shifted_res;
                                reg signed [16:0] sum;
                                shifted_res = round_shift16(ixform_out_w[i], 4);
                                sum = $signed({1'b0, 8'b0, pred_blk[i * 8 + xform_col]}) +
                                      shifted_res;
                                if (sum < 0)
                                    recon_blk[i * 8 + xform_col] <= 8'd0;
                                else if (sum > 255)
                                    recon_blk[i * 8 + xform_col] <= 8'd255;
                                else
                                    recon_blk[i * 8 + xform_col] <= sum[7:0];
                            end
                        end

                        if (xform_col < 7) begin
                            xform_col <= xform_col + 1;
                            top_state <= TS_IXFORM_COL;
                        end else begin
                            // Write reconstructed block to reference memory
                            ref_wr_idx <= 0;
                            top_state  <= TS_REF_WR;
                        end
                    end
                end

                // Inverse row transform (2D IDCT step 1)
                TS_IXFORM: begin
                    for (i = 0; i < 8; i = i + 1)
                        ixform_in[i] <= residual[xform_row * 8 + i];
                    ixform_start <= 1;
                    top_state    <= TS_RECON;
                end

                // Store first-pass inverse rows with AV1's rounded /2 shift
                TS_RECON: begin
                    if (ixform_done) begin
                        for (i = 0; i < 8; i = i + 1)
                            residual[xform_row * 8 + i] <= round_shift16(ixform_out_w[i], 1);

                        if (xform_row < 7) begin
                            xform_row <= xform_row + 1;
                            top_state <= TS_IXFORM;
                        end else begin
                            xform_col <= 0;
                            top_state <= TS_IXFORM_COL;
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
                        // The current software AV1 writer only emits zero-residual
                        // chroma blocks. Keep RTL reconstruction aligned with that
                        // path until real chroma residual coding is implemented.
                        for (i = 0; i < 16; i = i + 1)
                            chr_blk[i] <= 8'd128;
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
                            // Mirror the writer's 8x8 neighborhood context
                            // updates so later RTL-owned tile syntax can reuse
                            // the same partition/skip/mode state.
                            if ((blk_x << 1) < MI_COLS) begin
                                part_ctx_above[blk_x << 1] <= 8'd30;
                                skip_above[blk_x << 1] <= ~cur_block_has_coeff;
                                inter_above[blk_x << 1] <= use_inter;
                                ref_above[blk_x << 1] <= use_inter ? REF_LAST : REF_NONE;
                                mode_above[blk_x << 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                                dc_sign_above[blk_x << 1] <= cur_dc_sign_code;
                            end
                            if (((blk_x << 1) + 1) < MI_COLS) begin
                                part_ctx_above[(blk_x << 1) + 1] <= 8'd30;
                                skip_above[(blk_x << 1) + 1] <= ~cur_block_has_coeff;
                                inter_above[(blk_x << 1) + 1] <= use_inter;
                                ref_above[(blk_x << 1) + 1] <= use_inter ? REF_LAST : REF_NONE;
                                mode_above[(blk_x << 1) + 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                                dc_sign_above[(blk_x << 1) + 1] <= cur_dc_sign_code;
                            end
                            if ((blk_y << 1) < MI_ROWS) begin
                                part_ctx_left[blk_y << 1] <= 8'd30;
                                skip_left[blk_y << 1] <= ~cur_block_has_coeff;
                                inter_left[blk_y << 1] <= use_inter;
                                ref_left[blk_y << 1] <= use_inter ? REF_LAST : REF_NONE;
                                mode_left[blk_y << 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                                dc_sign_left[blk_y << 1] <= cur_dc_sign_code;
                            end
                            if (((blk_y << 1) + 1) < MI_ROWS) begin
                                part_ctx_left[(blk_y << 1) + 1] <= 8'd30;
                                skip_left[(blk_y << 1) + 1] <= ~cur_block_has_coeff;
                                inter_left[(blk_y << 1) + 1] <= use_inter;
                                ref_left[(blk_y << 1) + 1] <= use_inter ? REF_LAST : REF_NONE;
                                mode_left[(blk_y << 1) + 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                                dc_sign_left[(blk_y << 1) + 1] <= cur_dc_sign_code;
                            end
                            blk_inter_coded[blk_y * BLK_COLS + blk_x] <= use_inter;
                            blk_ref0[blk_y * BLK_COLS + blk_x] <= use_inter ? REF_LAST : REF_NONE;
                            blk_inter_mode[blk_y * BLK_COLS + blk_x] <=
                                use_inter ? REDUCED_INTER_GLOBALMV : REDUCED_INTER_NONE;
                            // Both chroma planes done
                            top_state <= TS_NEXT_BLK;
                        end
                    end
                end

                // Advance to next block
                TS_NEXT_BLK: begin
                    if (has_next_blk_morton) begin
                        blk_x    <= next_blk_x_morton;
                        blk_y    <= next_blk_y_morton;
                        top_state <= TS_PART_PREP;
                    end else begin
                        // All blocks processed
                        // Finalize entropy coder
                        ec_finalize <= 1;
                        top_state   <= TS_DONE;
                    end
                end

                TS_DONE: begin
                    // The entropy core only starts its buffered final flush on
                    // the cycle after ec_finalize is pulsed. Do not treat
                    // idle-before-flush as completion or the last entropy
                    // bytes can be dropped from the owned raw stream.
                    if (ec_done) begin
                        top_state <= TS_DONE_COMMIT;
                    end
                end

                TS_DONE_COMMIT: begin
                    // Back-patch the frame OBU size byte only after the final
                    // entropy byte has had a full cycle to commit into the
                    // unified output mux and byte counters.
                    manual_bs_wr   <= 1;
                    manual_bs_addr <= frame_obu_start_addr + 24'd1;
                    manual_bs_data <= frame_obu_payload_bytes[7:0];
                    top_state      <= TS_DONE_FINISH;
                end

                TS_DONE_FINISH: begin
                    done      <= 1;
                    top_state <= TS_IDLE;
                end
            endcase
        end
    end

endmodule
