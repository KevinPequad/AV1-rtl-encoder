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
    output wire [23:0] bs_bytes_written
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
    reg [3:0]  mode_above     [0:MI_COLS-1];
    reg [3:0]  mode_left      [0:MI_ROWS-1];

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
                2'd0: skip_icdf_flat = {224'd0, 16'd32768, 16'd31671};
                2'd1: skip_icdf_flat = {224'd0, 16'd32768, 16'd16515};
                default: skip_icdf_flat = {224'd0, 16'd32768, 16'd4576};
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
                6'd0:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd30466,16'd29093,16'd28165,16'd24189,16'd23244,16'd21825,16'd21110,16'd20682,16'd20218,16'd19338,16'd17027,16'd15588};
                6'd1:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd31409,16'd30172,16'd28658,16'd24434,16'd23032,16'd21888,16'd21444,16'd20719,16'd20303,16'd19516,16'd18066,16'd12016};
                6'd2:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd31567,16'd29929,16'd29336,16'd26160,16'd25620,16'd24133,16'd23239,16'd23055,16'd22788,16'd22296,16'd10771,16'd10052};
                6'd3:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32462,16'd30958,16'd29585,16'd24746,16'd22096,16'd19998,16'd19546,16'd19136,16'd18808,16'd16442,16'd15406,16'd14091};
                6'd4:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32475,16'd31073,16'd30261,16'd26437,16'd25583,16'd22391,16'd20033,16'd18609,16'd16501,16'd15603,16'd13265,16'd12122};
                6'd8:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd31436,16'd30482,16'd29014,16'd25381,16'd24023,16'd23089,16'd22760,16'd21832,16'd21440,16'd20848,16'd19585,16'd10023};
                6'd9:  kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd31794,16'd31276,16'd29905,16'd27610,16'd26423,16'd25913,16'd25795,16'd25066,16'd24886,16'd24560,16'd24099,16'd5983};
                6'd10: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd30492,16'd29090,16'd27915,16'd24469,16'd23405,16'd22170,16'd21607,16'd21077,16'd20728,16'd20177,16'd12781,16'd7444};
                6'd11: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32210,16'd31096,16'd29153,16'd24649,16'd19825,16'd18408,16'd18172,16'd17408,16'd17087,16'd15432,16'd14689,16'd8537};
                6'd12: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd31994,16'd30981,16'd29675,16'd26001,16'd24516,16'd21984,16'd20717,16'd17905,16'd16195,16'd15496,16'd14231,16'd7543};
                6'd16: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32077,16'd30305,16'd29538,16'd25729,16'd25055,16'd23401,16'd22577,16'd22312,16'd22004,16'd21383,16'd13591,16'd12613};
                6'd17: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd30907,16'd29211,16'd27743,16'd23219,16'd22062,16'd20695,16'd20147,16'd19604,16'd19230,16'd18506,16'd13470,16'd9687};
                6'd18: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32086,16'd30794,16'd30467,16'd28555,16'd28354,16'd27082,16'd26434,16'd26366,16'd26252,16'd26024,16'd6505,16'd6183};
                6'd19: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32252,16'd30287,16'd28975,16'd23878,16'd21523,16'd18561,16'd17924,16'd17565,16'd17224,16'd14954,16'd11734,16'd10718};
                6'd20: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32463,16'd30737,16'd30072,16'd26561,16'd25961,16'd21563,16'd19171,16'd18424,16'd17263,16'd16501,16'd9858,16'd9194};
                6'd24: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32409,16'd30917,16'd29696,16'd25060,16'd21419,16'd19724,16'd19315,16'd18778,16'd18381,16'd15488,16'd14399,16'd12602};
                6'd25: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32342,16'd31158,16'd29485,16'd25225,16'd19468,16'd18404,16'd18131,16'd17439,16'd17105,16'd14524,16'd13821,16'd8203};
                6'd26: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32018,16'd30078,16'd29118,16'd24605,16'd21538,16'd19070,16'd18425,16'd18012,16'd17643,16'd15004,16'd9731,16'd8451};
                6'd27: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32528,16'd31536,16'd30389,16'd26743,16'd18767,16'd17153,16'd16994,16'd16817,16'd16667,16'd9516,16'd9048,16'd7714};
                6'd28: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32485,16'd30983,16'd29953,16'd25769,16'd22718,16'd19108,16'd17943,16'd16652,16'd15317,16'd11496,16'd10280,16'd8843};
                6'd32: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32477,16'd31150,16'd30214,16'd26219,16'd25449,16'd22989,16'd20913,16'd19075,16'd16834,16'd15979,16'd13671,16'd12578};
                6'd33: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32277,16'd31143,16'd29653,16'd25380,16'd24236,16'd22207,16'd20863,16'd17756,16'd15892,16'd15080,16'd13626,16'd9563};
                6'd34: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32261,16'd30523,16'd29900,16'd26466,16'd25947,16'd22598,16'd20106,16'd19350,16'd18256,16'd17616,16'd8901,16'd8356};
                6'd35: kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32482,16'd30883,16'd29489,16'd24615,16'd22753,16'd18947,16'd18039,16'd17018,16'd16042,16'd13124,16'd11815,16'd10835};
                default:kf_y_mode_icdf_flat = {48'd0,16'd32768,16'd32593,16'd31802,16'd31355,16'd29180,16'd28776,16'd22903,16'd18657,16'd15386,16'd10509,16'd9859,16'd8288,16'd7618};
            endcase
        end
    endfunction

    function [255:0] angle_delta_icdf_flat;
        input [3:0] mode;
        begin
            case (mode)
                AV1_V_PRED:    angle_delta_icdf_flat = {192'd0,16'd22776,16'd7567,16'd5032,16'd2180};
                AV1_H_PRED:    angle_delta_icdf_flat = {192'd0,16'd23487,16'd8801,16'd5608,16'd2301};
                AV1_D45_PRED:  angle_delta_icdf_flat = {192'd0,16'd19354,16'd13699,16'd11018,16'd3780};
                AV1_D135_PRED: angle_delta_icdf_flat = {192'd0,16'd17138,16'd15147,16'd11226,16'd4581};
                AV1_D113_PRED: angle_delta_icdf_flat = {192'd0,16'd19588,16'd14509,16'd10927,16'd1737};
                AV1_D157_PRED: angle_delta_icdf_flat = {192'd0,16'd17650,16'd12485,16'd10176,16'd2664};
                AV1_D203_PRED: angle_delta_icdf_flat = {192'd0,16'd20341,16'd15453,16'd11096,16'd2240};
                default:       angle_delta_icdf_flat = {192'd0,16'd17676,16'd12459,16'd10428,16'd3605};
            endcase
        end
    endfunction

    function [255:0] if_y_mode_icdf_flat;
        begin
            if_y_mode_icdf_flat = {48'd0,16'd32768,16'd30852,16'd29984,16'd29701,16'd28152,16'd27364,16'd25527,16'd24649,16'd23950,16'd23318,16'd22631,16'd19845,16'd18673};
        end
    endfunction

    function [255:0] uv_mode_dc_icdf_flat;
        input [3:0] y_mode;
        begin
            case (y_mode)
                AV1_DC_PRED:     uv_mode_dc_icdf_flat = {240'd0,16'd10407};
                AV1_V_PRED:      uv_mode_dc_icdf_flat = {240'd0,16'd4532};
                AV1_H_PRED:      uv_mode_dc_icdf_flat = {240'd0,16'd5273};
                AV1_D45_PRED:    uv_mode_dc_icdf_flat = {240'd0,16'd6740};
                AV1_D135_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd4987};
                AV1_D113_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd5370};
                AV1_D157_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd4816};
                AV1_D203_PRED:   uv_mode_dc_icdf_flat = {240'd0,16'd6608};
                AV1_D67_PRED:    uv_mode_dc_icdf_flat = {240'd0,16'd5998};
                AV1_SMOOTH_PRED: uv_mode_dc_icdf_flat = {240'd0,16'd10660};
                AV1_PAETH_PRED:  uv_mode_dc_icdf_flat = {240'd0,16'd3144};
                default:         uv_mode_dc_icdf_flat = {240'd0,16'd10407};
            endcase
        end
    endfunction

    wire [1:0] cur_skip_ctx = get_skip_ctx_cur(blk_x, blk_y);
    wire       cur_block_skip = ~cur_block_has_coeff;
    wire [2:0] cur_kf_above_ctx = get_kf_mode_above_ctx_cur(blk_x, blk_y);
    wire [2:0] cur_kf_left_ctx  = get_kf_mode_left_ctx_cur(blk_x, blk_y);
    wire [255:0] cur_kf_y_icdf  = kf_y_mode_icdf_flat(cur_kf_above_ctx, cur_kf_left_ctx);
    wire [255:0] cur_if_y_icdf  = if_y_mode_icdf_flat();
    wire [255:0] cur_ang_icdf   = angle_delta_icdf_flat(best_intra_mode);
    wire [255:0] cur_uv_icdf    = uv_mode_dc_icdf_flat(best_intra_mode);

    wire [3:0] intra_eval_mode = intra_mode_from_idx(intra_eval_idx);

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
        .mode(intra_eval_mode),
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
    reg        manual_bs_wr;
    reg [23:0] manual_bs_addr;
    reg [7:0]  manual_bs_data;
    reg [23:0] frame_obu_start_addr;
    wire [23:0] frame_obu_payload_bytes =
        (total_bs_bytes > (frame_obu_start_addr + 24'd2)) ?
            (total_bs_bytes - frame_obu_start_addr - 24'd2) : 24'd0;

    // Mux bitstream and entropy coder output to memory
    assign bs_mem_wr   = manual_bs_wr | bs_byte_valid | ec_byte_valid;
    assign bs_mem_data = manual_bs_wr ? manual_bs_data :
                         (bs_byte_valid ? bs_byte_out : ec_byte_out);
    assign bs_mem_addr = manual_bs_wr ? manual_bs_addr : bs_wr_addr;
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
            best_intra_mode <= AV1_DC_PRED;
            intra_eval_idx  <= 4'd0;
            intra_best_sad  <= 18'h3FFFF;
            intra_cand_sad  <= 18'd0;
            frame_obu_start_addr <= 24'd0;
            for (i = 0; i < MI_COLS; i = i + 1) begin
                part_ctx_above[i] <= 8'd0;
                skip_above[i] <= 1'b0;
                mode_above[i] <= AV1_DC_PRED;
            end
            for (i = 0; i < MI_ROWS; i = i + 1) begin
                part_ctx_left[i] <= 8'd0;
                skip_left[i] <= 1'b0;
                mode_left[i] <= AV1_DC_PRED;
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
                        qindex      <= qindex_in;
                        frame_num   <= frame_num_in;
                        blk_x       <= 0;
                        blk_y       <= 0;
                        cur_block_has_coeff <= 1'b0;
                        for (i = 0; i < MI_COLS; i = i + 1) begin
                            part_ctx_above[i] <= 8'd0;
                            skip_above[i] <= 1'b0;
                            mode_above[i] <= AV1_DC_PRED;
                        end
                        for (i = 0; i < MI_ROWS; i = i + 1) begin
                            part_ctx_left[i] <= 8'd0;
                            skip_left[i] <= 1'b0;
                            mode_left[i] <= AV1_DC_PRED;
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
                    cur_block_has_coeff <= 1'b0;
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
                                top_state <= TS_PREDICT_INIT;
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
                                     (me_best_sad < INTRA_SAD_THRESHOLD);
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
                        if (((dc_only_in && proc_idx != 0) ? 16'sd0 : quant_coeff_out) != 16'sd0)
                            cur_block_has_coeff <= 1'b1;
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
                        if (!use_inter) begin
                            top_state <= TS_SYNTAX_YMODE;
                        end else if (cur_block_skip) begin
                            proc_idx  <= 0;
                            top_state <= TS_IQ_START;
                        end else begin
                            proc_idx  <= 0;
                            top_state <= TS_COEFF_SYM;
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
                            top_state <= TS_COEFF_SYM;
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
                        // Reconstruct: recon = clamp(pred + round_shift(inv_residual, 5), 0, 255)
                        // The current 8x8 RTL transform pair produces roughly a 32x
                        // round-trip gain, so we normalize by 5 bits here to match the
                        // normative AV1 decoder's reconstructed sample range.
                        for (i = 0; i < 8; i = i + 1) begin
                            begin
                                reg signed [16:0] shifted_res;
                                reg signed [16:0] sum;
                                shifted_res = (ixform_out_w[i] + 16'sd16) >>> 5;
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
                                mode_above[blk_x << 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                            end
                            if (((blk_x << 1) + 1) < MI_COLS) begin
                                part_ctx_above[(blk_x << 1) + 1] <= 8'd30;
                                skip_above[(blk_x << 1) + 1] <= ~cur_block_has_coeff;
                                mode_above[(blk_x << 1) + 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                            end
                            if ((blk_y << 1) < MI_ROWS) begin
                                part_ctx_left[blk_y << 1] <= 8'd30;
                                skip_left[blk_y << 1] <= ~cur_block_has_coeff;
                                mode_left[blk_y << 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                            end
                            if (((blk_y << 1) + 1) < MI_ROWS) begin
                                part_ctx_left[(blk_y << 1) + 1] <= 8'd30;
                                skip_left[(blk_y << 1) + 1] <= ~cur_block_has_coeff;
                                mode_left[(blk_y << 1) + 1] <= use_inter ? AV1_DC_PRED : best_intra_mode;
                            end
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
                        // Back-patch the frame OBU size byte for the current
                        // reduced single-byte LEB128 debug path. This keeps
                        // the RTL-owned raw stream aligned with the emitted
                        // frame payload length on the small bring-up cases.
                        manual_bs_wr   <= 1;
                        manual_bs_addr <= frame_obu_start_addr + 24'd1;
                        manual_bs_data <= frame_obu_payload_bytes[7:0];
                        done      <= 1;
                        top_state <= TS_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
