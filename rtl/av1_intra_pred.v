// av1_intra_pred.v - AV1 intra prediction for 4x4 / 8x8 blocks
// Supported AV1 luma mode IDs:
//   DC_PRED     = 0
//   V_PRED      = 1
//   H_PRED      = 2
//   D45_PRED    = 3
//   D135_PRED   = 4
//   D113_PRED   = 5
//   D157_PRED   = 6
//   D203_PRED   = 7
//   D67_PRED    = 8
//   SMOOTH_PRED = 9
//   PAETH_PRED  = 12
//
// Reference:
//   SVT-AV1 Source/Lib/Codec/definitions.h
//   SVT-AV1 Source/Lib/Codec/intra_prediction.c

module av1_intra_pred (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        is_4x4,
    input  wire [3:0]  mode,
    output reg         done,

    input  wire [7:0]  top  [0:7],
    input  wire [7:0]  left [0:7],
    input  wire [7:0]  top_left,
    input  wire        has_top,
    input  wire        has_left,

    output reg  [7:0]  pred [0:63]
);

    localparam [3:0] DC_PRED     = 4'd0;
    localparam [3:0] V_PRED      = 4'd1;
    localparam [3:0] H_PRED      = 4'd2;
    localparam [3:0] D45_PRED    = 4'd3;
    localparam [3:0] D135_PRED   = 4'd4;
    localparam [3:0] D113_PRED   = 4'd5;
    localparam [3:0] D157_PRED   = 4'd6;
    localparam [3:0] D203_PRED   = 4'd7;
    localparam [3:0] D67_PRED    = 4'd8;
    localparam [3:0] SMOOTH_PRED = 4'd9;
    localparam [3:0] PAETH_PRED  = 4'd12;

    reg [3:0] blk_size;
    reg [3:0] row;
    reg [1:0] stage;
    reg [12:0] dc_sum;
    reg [7:0] dc_val;
    reg        active;

    function [10:0] abs_diff11;
        input signed [10:0] a;
        input signed [10:0] b;
        begin
            abs_diff11 = (a > b) ? (a - b) : (b - a);
        end
    endfunction

    function [7:0] paeth_predict;
        input [7:0] left_px;
        input [7:0] top_px;
        input [7:0] top_left_px;
        reg signed [10:0] base;
        reg [10:0] d_left;
        reg [10:0] d_top;
        reg [10:0] d_top_left;
        begin
            base = $signed({3'b0, left_px}) + $signed({3'b0, top_px}) - $signed({3'b0, top_left_px});
            d_left = abs_diff11(base, $signed({3'b0, left_px}));
            d_top = abs_diff11(base, $signed({3'b0, top_px}));
            d_top_left = abs_diff11(base, $signed({3'b0, top_left_px}));

            if (d_left <= d_top && d_left <= d_top_left)
                paeth_predict = left_px;
            else if (d_top <= d_top_left)
                paeth_predict = top_px;
            else
                paeth_predict = top_left_px;
        end
    endfunction

    function [7:0] smooth_weight;
        input [3:0] size;
        input [3:0] idx;
        begin
            case (size)
                4: begin
                    case (idx)
                        0: smooth_weight = 8'd255;
                        1: smooth_weight = 8'd149;
                        2: smooth_weight = 8'd85;
                        3: smooth_weight = 8'd64;
                        default: smooth_weight = 8'd64;
                    endcase
                end
                8: begin
                    case (idx)
                        0: smooth_weight = 8'd255;
                        1: smooth_weight = 8'd197;
                        2: smooth_weight = 8'd146;
                        3: smooth_weight = 8'd105;
                        4: smooth_weight = 8'd73;
                        5: smooth_weight = 8'd50;
                        6: smooth_weight = 8'd37;
                        7: smooth_weight = 8'd32;
                        default: smooth_weight = 8'd32;
                    endcase
                end
                default: smooth_weight = 8'd128;
            endcase
        end
    endfunction

    function [7:0] smooth_predict;
        input [3:0] size;
        input [3:0] r;
        input [3:0] c;
        input [7:0] top_px;
        input [7:0] left_px;
        input [7:0] right_px;
        input [7:0] below_px;
        reg [7:0] w_row;
        reg [7:0] w_col;
        reg [18:0] acc;
        begin
            w_row = smooth_weight(size, r);
            w_col = smooth_weight(size, c);
            acc = w_row * top_px +
                  (8'd255 - w_row) * below_px +
                  w_col * left_px +
                  (8'd255 - w_col) * right_px;
            smooth_predict = (acc + 19'd256) >> 9;
        end
    endfunction

    function [5:0] scaled_offset;
        input [9:0] mult;
        input [4:0] step;
        begin
            scaled_offset = (mult * step + 10'd128) >> 8;
        end
    endfunction

    integer i;
    reg [12:0] sum_tmp;
    reg [7:0] top_px;
    reg [7:0] left_px;
    reg [7:0] right_px;
    reg [7:0] below_px;
    reg [7:0] top_left_px;
    reg signed [6:0] proj_idx;
    reg [5:0] dir_off;
    reg [5:0] left_idx;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done   <= 1'b0;
            active <= 1'b0;
            stage  <= 2'd0;
            row    <= 4'd0;
            dc_sum <= 13'd0;
            dc_val <= 8'd128;
        end else begin
            done <= 1'b0;

            if (start && !active) begin
                active   <= 1'b1;
                blk_size <= is_4x4 ? 4'd4 : 4'd8;
                stage    <= 2'd0;
                row      <= 4'd0;
                dc_sum   <= 13'd0;
            end else if (active) begin
                case (stage)
                    2'd0: begin
                        sum_tmp = 13'd0;
                        for (i = 0; i < 8; i = i + 1) begin
                            if (i < blk_size) begin
                                if (has_top)
                                    sum_tmp = sum_tmp + {5'd0, top[i]};
                                if (has_left)
                                    sum_tmp = sum_tmp + {5'd0, left[i]};
                            end
                        end
                        dc_sum <= sum_tmp;
                        stage  <= 2'd1;
                    end

                    2'd1: begin
                        if (has_top && has_left)
                            dc_val <= (dc_sum + blk_size) / (blk_size << 1);
                        else if (has_top || has_left)
                            dc_val <= (dc_sum + (blk_size >> 1)) / blk_size;
                        else
                            dc_val <= 8'd128;
                        stage <= 2'd2;
                    end

                    2'd2: begin
                        right_px = has_top ? top[blk_size - 1] : 8'd128;
                        below_px = has_left ? left[blk_size - 1] : 8'd128;
                        top_left_px = (has_top && has_left) ? top_left : 8'd128;

                        for (i = 0; i < 8; i = i + 1) begin
                            if (i < blk_size) begin
                                top_px = has_top ? top[i] : 8'd128;
                                left_px = has_left ? left[row] : 8'd128;

                                case (mode)
                                    DC_PRED: begin
                                        pred[row * blk_size + i] <= dc_val;
                                    end

                                    V_PRED: begin
                                        pred[row * blk_size + i] <= top_px;
                                    end

                                    H_PRED: begin
                                        pred[row * blk_size + i] <= left_px;
                                    end

                                    D45_PRED: begin
                                        dir_off = row + 1'b1;
                                        proj_idx = $signed({3'b0, i[3:0]}) + $signed({2'b0, dir_off});
                                        if (has_top) begin
                                            if (proj_idx >= blk_size)
                                                pred[row * blk_size + i] <= top[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= top[proj_idx[3:0]];
                                        end else if (has_left) begin
                                            pred[row * blk_size + i] <= left[0];
                                        end else begin
                                            pred[row * blk_size + i] <= 8'd128;
                                        end
                                    end

                                    D67_PRED: begin
                                        dir_off = scaled_offset(10'd106, {2'b0, row} + 5'd1);
                                        proj_idx = $signed({3'b0, i[3:0]}) + $signed({1'b0, dir_off});
                                        if (has_top) begin
                                            if (proj_idx >= blk_size)
                                                pred[row * blk_size + i] <= top[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= top[proj_idx[3:0]];
                                        end else if (has_left) begin
                                            pred[row * blk_size + i] <= left[0];
                                        end else begin
                                            pred[row * blk_size + i] <= 8'd128;
                                        end
                                    end

                                    D113_PRED: begin
                                        dir_off = scaled_offset(10'd106, {2'b0, row} + 5'd1);
                                        proj_idx = $signed({3'b0, i[3:0]}) - $signed({1'b0, dir_off});
                                        if (proj_idx >= 0 && has_top) begin
                                            pred[row * blk_size + i] <= top[proj_idx[3:0]];
                                        end else if (has_left) begin
                                            left_idx = (proj_idx < 0) ? (-proj_idx - 1'b1) : 6'd0;
                                            if (left_idx >= blk_size)
                                                pred[row * blk_size + i] <= left[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= left[left_idx[3:0]];
                                        end else if (has_top) begin
                                            pred[row * blk_size + i] <= top[0];
                                        end else begin
                                            pred[row * blk_size + i] <= 8'd128;
                                        end
                                    end

                                    D135_PRED: begin
                                        dir_off = row + 1'b1;
                                        proj_idx = $signed({3'b0, i[3:0]}) - $signed({2'b0, dir_off});
                                        if (proj_idx >= 0 && has_top) begin
                                            pred[row * blk_size + i] <= top[proj_idx[3:0]];
                                        end else if (has_left) begin
                                            left_idx = (proj_idx < 0) ? (-proj_idx - 1'b1) : 6'd0;
                                            if (left_idx >= blk_size)
                                                pred[row * blk_size + i] <= left[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= left[left_idx[3:0]];
                                        end else if (has_top) begin
                                            pred[row * blk_size + i] <= top[0];
                                        end else begin
                                            pred[row * blk_size + i] <= 8'd128;
                                        end
                                    end

                                    D157_PRED: begin
                                        dir_off = scaled_offset(10'd603, {2'b0, row} + 5'd1);
                                        proj_idx = $signed({3'b0, i[3:0]}) - $signed({1'b0, dir_off});
                                        if (proj_idx >= 0 && has_top) begin
                                            if (proj_idx >= blk_size)
                                                pred[row * blk_size + i] <= top[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= top[proj_idx[3:0]];
                                        end else if (has_left) begin
                                            left_idx = (proj_idx < 0) ? (-proj_idx - 1'b1) : 6'd0;
                                            if (left_idx >= blk_size)
                                                pred[row * blk_size + i] <= left[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= left[left_idx[3:0]];
                                        end else if (has_top) begin
                                            pred[row * blk_size + i] <= top[0];
                                        end else begin
                                            pred[row * blk_size + i] <= 8'd128;
                                        end
                                    end

                                    D203_PRED: begin
                                        dir_off = scaled_offset(10'd106, {1'b0, i[3:0]} + 5'd1);
                                        left_idx = row + dir_off;
                                        if (has_left) begin
                                            if (left_idx >= blk_size)
                                                pred[row * blk_size + i] <= left[blk_size - 1];
                                            else
                                                pred[row * blk_size + i] <= left[left_idx[3:0]];
                                        end else if (has_top) begin
                                            pred[row * blk_size + i] <= top[0];
                                        end else begin
                                            pred[row * blk_size + i] <= 8'd128;
                                        end
                                    end

                                    PAETH_PRED: begin
                                        if (has_top && has_left)
                                            pred[row * blk_size + i] <= paeth_predict(left_px, top_px, top_left_px);
                                        else if (has_left)
                                            pred[row * blk_size + i] <= left_px;
                                        else if (has_top)
                                            pred[row * blk_size + i] <= top_px;
                                        else
                                            pred[row * blk_size + i] <= 8'd128;
                                    end

                                    SMOOTH_PRED: begin
                                        pred[row * blk_size + i] <= smooth_predict(
                                            blk_size, row, i[3:0], top_px, left_px, right_px, below_px
                                        );
                                    end

                                    default: begin
                                        pred[row * blk_size + i] <= 8'd128;
                                    end
                                endcase
                            end
                        end

                        if (row == blk_size - 1) begin
                            active <= 1'b0;
                            done   <= 1'b1;
                            stage  <= 2'd0;
                        end else begin
                            row <= row + 1'b1;
                        end
                    end
                endcase
            end
        end
    end

endmodule
