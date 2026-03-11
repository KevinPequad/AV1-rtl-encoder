// av1_me.v — AV1 Motion Estimation (Full-pel SAD search)
// Searches a reference frame for the best matching 8x8 block.
// Uses a diamond/full search pattern within ±search_range.
// Reference: SVT-AV1/Source/Lib/Codec/av1me.c

module av1_me #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720,
    parameter SEARCH_RANGE = 16    // ±16 pixels
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        zero_mv_only,
    output reg         done,

    // Current block position (pixel coordinates)
    input  wire [10:0] cur_x,
    input  wire [10:0] cur_y,

    // Current block pixels (8x8 = 64 bytes)
    input  wire [7:0]  cur_blk [0:63],

    // Reference frame memory interface
    output reg  [19:0] ref_mem_addr,
    input  wire [7:0]  ref_mem_data,

    // Best match results
    output reg signed [8:0] best_mvx,
    output reg signed [8:0] best_mvy,
    output reg        [17:0] best_sad
);

    localparam LUMA_SIZE = FRAME_WIDTH * FRAME_HEIGHT;

    reg [4:0]  state;
    localparam S_IDLE      = 5'd0;
    localparam S_INIT      = 5'd1;
    localparam S_FETCH_REF = 5'd2;
    localparam S_WAIT_MEM  = 5'd3;
    localparam S_COMPUTE   = 5'd4;
    localparam S_NEXT_PIX  = 5'd5;
    localparam S_NEXT_MV   = 5'd6;
    localparam S_DONE      = 5'd7;

    reg signed [8:0] mv_x, mv_y;
    reg signed [8:0] mv_x_min, mv_x_max;
    reg signed [8:0] mv_y_min, mv_y_max;
    reg [5:0]  pix_idx;       // 0..63 within 8x8 block
    reg [17:0] cur_sad;
    reg [17:0] sad_after_pixel;
    reg [7:0]  ref_pixel;
    reg [10:0] ref_x, ref_y;
    reg        zero_mv_pending;

    localparam signed [11:0] SEARCH_RANGE_S = SEARCH_RANGE;
    localparam signed [11:0] BLOCK_SIZE_S   = 12'sd8;
    localparam signed [11:0] FRAME_WIDTH_S  = FRAME_WIDTH;
    localparam signed [11:0] FRAME_HEIGHT_S = FRAME_HEIGHT;

    wire signed [10:0] cand_x = $signed({1'b0, cur_x}) + mv_x;
    wire signed [10:0] cand_y = $signed({1'b0, cur_y}) + mv_y;
    wire signed [11:0] cur_x_s = $signed({1'b0, cur_x});
    wire signed [11:0] cur_y_s = $signed({1'b0, cur_y});
    wire signed [11:0] valid_min_x_full_w =
        (cur_x_s < SEARCH_RANGE_S) ? -cur_x_s : -SEARCH_RANGE_S;
    wire signed [11:0] valid_min_y_full_w =
        (cur_y_s < SEARCH_RANGE_S) ? -cur_y_s : -SEARCH_RANGE_S;
    wire signed [11:0] valid_max_x_full_w =
        ((cur_x_s + BLOCK_SIZE_S + SEARCH_RANGE_S) > FRAME_WIDTH_S) ?
            (FRAME_WIDTH_S - BLOCK_SIZE_S - cur_x_s) : SEARCH_RANGE_S;
    wire signed [11:0] valid_max_y_full_w =
        ((cur_y_s + BLOCK_SIZE_S + SEARCH_RANGE_S) > FRAME_HEIGHT_S) ?
            (FRAME_HEIGHT_S - BLOCK_SIZE_S - cur_y_s) : SEARCH_RANGE_S;
    wire signed [11:0] valid_min_x_w = zero_mv_only ? 12'sd0 : valid_min_x_full_w;
    wire signed [11:0] valid_min_y_w = zero_mv_only ? 12'sd0 : valid_min_y_full_w;
    wire signed [11:0] valid_max_x_w = zero_mv_only ? 12'sd0 : valid_max_x_full_w;
    wire signed [11:0] valid_max_y_w = zero_mv_only ? 12'sd0 : valid_max_y_full_w;
    wire zero_mv_valid_w =
        (valid_min_x_w <= 0 && valid_max_x_w >= 0 &&
         valid_min_y_w <= 0 && valid_max_y_w >= 0);
    wire [7:0] abs_diff_w =
        (cur_blk[pix_idx] > ref_mem_data) ?
            (cur_blk[pix_idx] - ref_mem_data) :
            (ref_mem_data - cur_blk[pix_idx]);
    wire [17:0] cur_sad_next_w = cur_sad + abs_diff_w;

    function [17:0] advance_raster_pair;
        input signed [8:0] cur_mv_x;
        input signed [8:0] cur_mv_y;
        input signed [8:0] min_mv_x;
        input signed [8:0] max_mv_x;
        reg signed [8:0] next_mv_x;
        reg signed [8:0] next_mv_y;
        begin
            if (cur_mv_x < max_mv_x) begin
                next_mv_x = cur_mv_x + 1'b1;
                next_mv_y = cur_mv_y;
            end else begin
                next_mv_x = min_mv_x;
                next_mv_y = cur_mv_y + 1'b1;
            end
            advance_raster_pair = {next_mv_y, next_mv_x};
        end
    endfunction

    function [17:0] advance_raster_skip_zero;
        input signed [8:0] cur_mv_x;
        input signed [8:0] cur_mv_y;
        input signed [8:0] min_mv_x;
        input signed [8:0] max_mv_x;
        input signed [8:0] max_mv_y;
        input              skip_zero;
        reg [17:0] pair;
        reg signed [8:0] next_mv_x;
        reg signed [8:0] next_mv_y;
        begin
            pair = advance_raster_pair(cur_mv_x, cur_mv_y, min_mv_x, max_mv_x);
            next_mv_y = pair[17:9];
            next_mv_x = pair[8:0];
            // Zero MV is evaluated first via zero_mv_pending. Skip it during the
            // raster scan unless it is also the final legal candidate.
            if (skip_zero && next_mv_x == 0 && next_mv_y == 0 &&
                !(next_mv_x == max_mv_x && next_mv_y == max_mv_y))
                pair = advance_raster_pair(next_mv_x, next_mv_y, min_mv_x, max_mv_x);
            advance_raster_skip_zero = pair;
        end
    endfunction

    wire [17:0] next_mv_pair_w =
        advance_raster_skip_zero(mv_x, mv_y, mv_x_min, mv_x_max, mv_y_max, 1'b1);
    wire signed [8:0] next_mv_x_w = next_mv_pair_w[8:0];
    wire signed [8:0] next_mv_y_w = next_mv_pair_w[17:9];
    wire [17:0] first_scan_pair_w =
        advance_raster_skip_zero(mv_x_min, mv_y_min, mv_x_min, mv_x_max, mv_y_max, 1'b1);
    wire signed [8:0] first_scan_x_w = first_scan_pair_w[8:0];
    wire signed [8:0] first_scan_y_w = first_scan_pair_w[17:9];
    wire single_candidate_w = (mv_x_min == mv_x_max) && (mv_y_min == mv_y_max);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done     <= 0;
            state    <= S_IDLE;
            best_mvx <= 0;
            best_mvy <= 0;
            best_sad <= 18'h3FFFF;
            mv_x_min <= 0;
            mv_x_max <= 0;
            mv_y_min <= 0;
            mv_y_max <= 0;
            cur_sad  <= 0;
            sad_after_pixel <= 0;
            zero_mv_pending <= 0;
        end else begin
            done <= 0;

            case (state)
                S_IDLE: begin
                    if (start) begin
                        state    <= S_INIT;
                        mv_x_min <= valid_min_x_w[8:0];
                        mv_x_max <= valid_max_x_w[8:0];
                        mv_y_min <= valid_min_y_w[8:0];
                        mv_y_max <= valid_max_y_w[8:0];
                        mv_x     <= zero_mv_valid_w ? 9'sd0 : valid_min_x_w[8:0];
                        mv_y     <= zero_mv_valid_w ? 9'sd0 : valid_min_y_w[8:0];
                        best_sad <= 18'h3FFFF;
                        best_mvx <= 0;
                        best_mvy <= 0;
                        zero_mv_pending <= zero_mv_valid_w;
                    end
                end

                S_INIT: begin
                    pix_idx <= 0;
                    cur_sad <= 0;
                    sad_after_pixel <= 0;
                    state   <= S_FETCH_REF;
                end

                S_FETCH_REF: begin
                    // Compute address for current pixel in reference
                    ref_x <= cand_x + (pix_idx & 6'd7);  // pix_idx % 8
                    ref_y <= cand_y + (pix_idx >> 3);     // pix_idx / 8
                    ref_mem_addr <= (cand_y + (pix_idx >> 3)) * FRAME_WIDTH +
                                    (cand_x + (pix_idx & 6'd7));
                    state <= S_WAIT_MEM;
                end

                S_WAIT_MEM: begin
                    // One cycle memory latency
                    state <= S_COMPUTE;
                end

                S_COMPUTE: begin
                    // Accumulate SAD
                    ref_pixel <= ref_mem_data;
                    cur_sad <= cur_sad_next_w;
                    sad_after_pixel <= cur_sad_next_w;
                    state <= S_NEXT_PIX;
                end

                S_NEXT_PIX: begin
                    // Early termination if SAD already exceeds best
                    if (sad_after_pixel >= best_sad) begin
                        state <= S_NEXT_MV;
                    end else if (pix_idx == 6'd63) begin
                        // Finished all 64 pixels
                        if (sad_after_pixel < best_sad) begin
                            best_sad <= sad_after_pixel;
                            best_mvx <= mv_x;
                            best_mvy <= mv_y;
                        end
                        state <= S_NEXT_MV;
                    end else begin
                        pix_idx <= pix_idx + 1;
                        state   <= S_FETCH_REF;
                    end
                end

                S_NEXT_MV: begin
                    if (best_sad == 18'd0) begin
                        state <= S_DONE;
                        zero_mv_pending <= 0;
                    end else if (zero_mv_pending) begin
                        zero_mv_pending <= 0;
                        if (single_candidate_w) begin
                            state <= S_DONE;
                        end else if (mv_x_min == 9'sd0 && mv_y_min == 9'sd0) begin
                            mv_x  <= first_scan_x_w;
                            mv_y  <= first_scan_y_w;
                            state <= S_INIT;
                        end else begin
                            mv_x  <= mv_x_min;
                            mv_y  <= mv_y_min;
                            state <= S_INIT;
                        end
                    end else if (mv_x == mv_x_max && mv_y == mv_y_max) begin
                        state <= S_DONE;
                    end else begin
                        mv_x <= next_mv_x_w;
                        mv_y <= next_mv_y_w;
                        state <= S_INIT;
                    end
                end

                S_DONE: begin
                    done  <= 1;
                    state <= S_IDLE;
                end
            endcase
        end
    end

endmodule
