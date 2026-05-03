// av1_me.v — AV1 Motion Estimation with half-pel q3 refinement
// Searches full-pel 8x8 candidates, then evaluates a reduced AV1 regular-filter
// half-pel refinement around the best full-pel vector. Full-pel outputs are kept
// for legacy/debug visibility; q3 outputs are used by the encoder syntax/predictor.

module av1_me #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720,
    parameter SEARCH_RANGE = 16
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        zero_mv_only,
    output reg         done,

    input  wire [10:0] cur_x,
    input  wire [10:0] cur_y,
    input  wire [7:0]  cur_blk [0:63],

    output wire [19:0] ref_mem_addr,
    input  wire [7:0]  ref_mem_data,

    output reg signed [8:0]  best_mvx,
    output reg signed [8:0]  best_mvy,
    output reg signed [15:0] best_mvx_q3,
    output reg signed [15:0] best_mvy_q3,
    output reg        [17:0] best_sad
);

    localparam signed [11:0] SEARCH_RANGE_S = SEARCH_RANGE;
    localparam signed [11:0] BLOCK_SIZE_S   = 12'sd8;
    localparam signed [11:0] FRAME_WIDTH_S  = FRAME_WIDTH;
    localparam signed [11:0] FRAME_HEIGHT_S = FRAME_HEIGHT;

    localparam [4:0]
        S_IDLE      = 5'd0,
        S_INIT      = 5'd1,
        S_FETCH_REF = 5'd2,
        S_WAIT_MEM  = 5'd3,
        S_COMPUTE   = 5'd4,
        S_NEXT_PIX  = 5'd5,
        S_NEXT_MV   = 5'd6,
        S_SUB_START = 5'd7,
        S_SUB_WAIT  = 5'd8,
        S_DONE      = 5'd9;

    reg [4:0] state;
    reg [19:0] ref_mem_addr_r;
    reg signed [8:0] mv_x, mv_y;
    reg signed [8:0] mv_x_min, mv_x_max;
    reg signed [8:0] mv_y_min, mv_y_max;
    reg [5:0] pix_idx;
    reg [17:0] cur_sad;
    reg [17:0] sad_after_pixel;
    reg zero_mv_pending;

    reg [3:0] sub_idx;
    reg sub_start;
    reg signed [15:0] sub_cand_mvx_q3;
    reg signed [15:0] sub_cand_mvy_q3;
    reg [17:0] sub_sad_calc;
    integer si;

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

    wire sub_active = (state == S_SUB_START) || (state == S_SUB_WAIT);
    wire [19:0] sub_ref_mem_addr;
    wire sub_done;
    wire [7:0] sub_pred [0:63];
    assign ref_mem_addr = sub_active ? sub_ref_mem_addr : ref_mem_addr_r;

    av1_inter_pred #(
        .FRAME_WIDTH(FRAME_WIDTH),
        .FRAME_HEIGHT(FRAME_HEIGHT)
    ) u_subpel_pred (
        .clk(clk),
        .rst_n(rst_n),
        .start(sub_start),
        .cur_x(cur_x),
        .cur_y(cur_y),
        .mv_x_q3(sub_cand_mvx_q3),
        .mv_y_q3(sub_cand_mvy_q3),
        .ref_mem_addr(sub_ref_mem_addr),
        .ref_mem_data(ref_mem_data),
        .done(sub_done),
        .pred(sub_pred)
    );

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
            if (skip_zero && next_mv_x == 0 && next_mv_y == 0 &&
                !(next_mv_x == max_mv_x && next_mv_y == max_mv_y))
                pair = advance_raster_pair(next_mv_x, next_mv_y, min_mv_x, max_mv_x);
            advance_raster_skip_zero = pair;
        end
    endfunction

    function signed [15:0] sub_candidate_x;
        input [3:0] idx;
        begin
            case (idx)
                4'd1, 4'd5, 4'd6: sub_candidate_x = ($signed(best_mvx) <<< 3) + 16'sd4;
                4'd2, 4'd7, 4'd8: sub_candidate_x = ($signed(best_mvx) <<< 3) - 16'sd4;
                default: sub_candidate_x = $signed(best_mvx) <<< 3;
            endcase
        end
    endfunction

    function signed [15:0] sub_candidate_y;
        input [3:0] idx;
        begin
            case (idx)
                4'd3, 4'd5, 4'd7: sub_candidate_y = ($signed(best_mvy) <<< 3) + 16'sd4;
                4'd4, 4'd6, 4'd8: sub_candidate_y = ($signed(best_mvy) <<< 3) - 16'sd4;
                default: sub_candidate_y = $signed(best_mvy) <<< 3;
            endcase
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
            best_mvx_q3 <= 0;
            best_mvy_q3 <= 0;
            best_sad <= 18'h3FFFF;
            ref_mem_addr_r <= 20'd0;
            mv_x_min <= 0;
            mv_x_max <= 0;
            mv_y_min <= 0;
            mv_y_max <= 0;
            cur_sad  <= 0;
            sad_after_pixel <= 0;
            zero_mv_pending <= 0;
            sub_idx <= 4'd0;
            sub_start <= 1'b0;
            sub_cand_mvx_q3 <= 16'sd0;
            sub_cand_mvy_q3 <= 16'sd0;
        end else begin
            done <= 0;
            sub_start <= 1'b0;

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
                        best_mvx_q3 <= 0;
                        best_mvy_q3 <= 0;
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
                    ref_mem_addr_r <= (cand_y + (pix_idx >> 3)) * FRAME_WIDTH +
                                      (cand_x + (pix_idx & 6'd7));
                    state <= S_WAIT_MEM;
                end

                S_WAIT_MEM: begin
                    state <= S_COMPUTE;
                end

                S_COMPUTE: begin
                    cur_sad <= cur_sad_next_w;
                    sad_after_pixel <= cur_sad_next_w;
                    state <= S_NEXT_PIX;
                end

                S_NEXT_PIX: begin
                    if (sad_after_pixel >= best_sad) begin
                        state <= S_NEXT_MV;
                    end else if (pix_idx == 6'd63) begin
                        if (sad_after_pixel < best_sad) begin
                            best_sad <= sad_after_pixel;
                            best_mvx <= mv_x;
                            best_mvy <= mv_y;
                            best_mvx_q3 <= $signed(mv_x) <<< 3;
                            best_mvy_q3 <= $signed(mv_y) <<< 3;
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
                            sub_idx <= zero_mv_only ? 4'd9 : 4'd0;
                            state <= zero_mv_only ? S_DONE : S_SUB_START;
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
                        sub_idx <= zero_mv_only ? 4'd9 : 4'd0;
                        state <= zero_mv_only ? S_DONE : S_SUB_START;
                    end else begin
                        mv_x <= next_mv_x_w;
                        mv_y <= next_mv_y_w;
                        state <= S_INIT;
                    end
                end

                S_SUB_START: begin
                    if (sub_idx >= 4'd9) begin
                        state <= S_DONE;
                    end else begin
                        sub_cand_mvx_q3 <= sub_candidate_x(sub_idx);
                        sub_cand_mvy_q3 <= sub_candidate_y(sub_idx);
                        sub_start <= 1'b1;
                        state <= S_SUB_WAIT;
                    end
                end

                S_SUB_WAIT: begin
                    if (sub_done) begin
                        sub_sad_calc = 18'd0;
                        for (si = 0; si < 64; si = si + 1) begin
                            if (cur_blk[si] > sub_pred[si])
                                sub_sad_calc = sub_sad_calc + (cur_blk[si] - sub_pred[si]);
                            else
                                sub_sad_calc = sub_sad_calc + (sub_pred[si] - cur_blk[si]);
                        end
                        if (sub_sad_calc < best_sad) begin
                            best_sad <= sub_sad_calc;
                            best_mvx_q3 <= sub_cand_mvx_q3;
                            best_mvy_q3 <= sub_cand_mvy_q3;
                        end
                        sub_idx <= sub_idx + 1'b1;
                        state <= S_SUB_START;
                    end
                end

                S_DONE: begin
                    done  <= 1;
                    state <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
