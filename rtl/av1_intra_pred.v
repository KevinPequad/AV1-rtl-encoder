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
    input  wire [7:0]  top_right [0:7],
    input  wire [7:0]  bottom_left [0:7],
    input  wire [7:0]  top_left,
    input  wire        has_top,
    input  wire        has_left,
    input  wire        has_top_right,
    input  wire        has_bottom_left,

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

    function [7:0] interp_w32;
        input [7:0] a;
        input [7:0] b;
        input [5:0] shift;
        reg [15:0] acc;
        begin
            acc = a * (6'd32 - shift) + b * shift;
            interp_w32 = (acc + 16'd16) >> 5;
        end
    endfunction

    function [8:0] mode_to_angle;
        input [3:0] pred_mode;
        begin
            case (pred_mode)
                V_PRED:    mode_to_angle = 9'd90;
                H_PRED:    mode_to_angle = 9'd180;
                D45_PRED:  mode_to_angle = 9'd45;
                D135_PRED: mode_to_angle = 9'd135;
                D113_PRED: mode_to_angle = 9'd113;
                D157_PRED: mode_to_angle = 9'd157;
                D203_PRED: mode_to_angle = 9'd203;
                D67_PRED:  mode_to_angle = 9'd67;
                default:   mode_to_angle = 9'd0;
            endcase
        end
    endfunction

    function [10:0] dr_intra_derivative;
        input [8:0] angle;
        begin
            case (angle)
                9'd3:  dr_intra_derivative = 11'd1023;
                9'd6:  dr_intra_derivative = 11'd547;
                9'd9:  dr_intra_derivative = 11'd372;
                9'd14: dr_intra_derivative = 11'd273;
                9'd17: dr_intra_derivative = 11'd215;
                9'd20: dr_intra_derivative = 11'd178;
                9'd23: dr_intra_derivative = 11'd151;
                9'd26: dr_intra_derivative = 11'd132;
                9'd29: dr_intra_derivative = 11'd116;
                9'd32: dr_intra_derivative = 11'd102;
                9'd36: dr_intra_derivative = 11'd90;
                9'd39: dr_intra_derivative = 11'd80;
                9'd42: dr_intra_derivative = 11'd71;
                9'd45: dr_intra_derivative = 11'd64;
                9'd48: dr_intra_derivative = 11'd57;
                9'd51: dr_intra_derivative = 11'd51;
                9'd54: dr_intra_derivative = 11'd45;
                9'd58: dr_intra_derivative = 11'd40;
                9'd61: dr_intra_derivative = 11'd35;
                9'd64: dr_intra_derivative = 11'd31;
                9'd67: dr_intra_derivative = 11'd27;
                9'd70: dr_intra_derivative = 11'd23;
                9'd74: dr_intra_derivative = 11'd19;
                9'd77: dr_intra_derivative = 11'd15;
                9'd80: dr_intra_derivative = 11'd11;
                9'd84: dr_intra_derivative = 11'd7;
                9'd87: dr_intra_derivative = 11'd3;
                default: dr_intra_derivative = 11'd0;
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
                  (9'd256 - w_row) * below_px +
                  w_col * left_px +
                  (9'd256 - w_col) * right_px;
            smooth_predict = (acc + 19'd256) >> 9;
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
    reg [7:0] above_ref [0:16];
    reg [7:0] left_ref [0:16];
    reg [4:0] max_base;
    reg [4:0] num_top_ref;
    reg [4:0] num_left_ref;
    reg [8:0] p_angle;
    reg        need_above_ref;
    reg        need_left_ref;
    reg        need_above_left_ref;
    reg        need_right_ref;
    reg        need_bottom_ref;
    reg [10:0] dx;
    reg [10:0] dy;
    reg signed [12:0] dir_x;
    reg signed [12:0] dir_y;
    reg signed [8:0] base_x;
    reg signed [8:0] base_y;
    reg [5:0] shift_amt;
    reg [7:0] samp_a;
    reg [7:0] samp_b;

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
                        right_px = has_top ? top[blk_size - 1] :
                                   (has_left ? left[0] : 8'd127);
                        below_px = has_left ? left[blk_size - 1] :
                                   (has_top ? top[0] : 8'd129);
                        top_left_px = (has_top && has_left) ? top_left :
                                      (has_top ? top[0] :
                                      (has_left ? left[0] : 8'd128));
                        max_base = (blk_size << 1) - 1;
                        p_angle = mode_to_angle(mode);
                        need_above_ref = 1'b0;
                        need_left_ref = 1'b0;
                        need_above_left_ref = 1'b0;
                        need_right_ref = 1'b0;
                        need_bottom_ref = 1'b0;
                        num_top_ref = blk_size;
                        num_left_ref = blk_size;
                        dx = 11'd0;
                        dy = 11'd0;

                        for (i = 0; i < 17; i = i + 1) begin
                            above_ref[i] = 8'd128;
                            left_ref[i] = 8'd128;
                        end

                        if (mode == V_PRED || mode == H_PRED || mode == D45_PRED ||
                            mode == D135_PRED || mode == D113_PRED ||
                            mode == D157_PRED || mode == D203_PRED ||
                            mode == D67_PRED) begin
                            if (p_angle <= 9'd90) begin
                                need_above_ref = 1'b1;
                                need_left_ref = 1'b0;
                            end else if (p_angle < 9'd180) begin
                                need_above_ref = 1'b1;
                                need_left_ref = 1'b1;
                            end else begin
                                need_above_ref = 1'b0;
                                need_left_ref = 1'b1;
                            end
                            need_above_left_ref = 1'b1;
                            need_right_ref = (p_angle < 9'd90);
                            need_bottom_ref = (p_angle > 9'd180);
                            num_top_ref = blk_size + (need_right_ref ? blk_size : 0);
                            num_left_ref = blk_size + (need_bottom_ref ? blk_size : 0);

                            if (p_angle < 9'd90)
                                dx = dr_intra_derivative(p_angle);
                            else if (p_angle > 9'd90 && p_angle < 9'd180)
                                dx = dr_intra_derivative(9'd180 - p_angle);
                            else
                                dx = 11'd0;

                            if (p_angle > 9'd90 && p_angle < 9'd180)
                                dy = dr_intra_derivative(p_angle - 9'd90);
                            else if (p_angle > 9'd180)
                                dy = dr_intra_derivative(9'd270 - p_angle);
                            else
                                dy = 11'd0;

                            if (need_above_ref) begin
                                if (has_top) begin
                                    for (i = 0; i < 16; i = i + 1) begin
                                        if (i < num_top_ref) begin
                                            if (i < blk_size)
                                                above_ref[i + 1] = top[i];
                                            else if (has_top_right)
                                                above_ref[i + 1] = top_right[i - blk_size];
                                            else
                                                above_ref[i + 1] = top[blk_size - 1];
                                        end
                                    end
                                end else if (has_left) begin
                                    for (i = 0; i < 16; i = i + 1) begin
                                        if (i < num_top_ref)
                                            above_ref[i + 1] = left[0];
                                    end
                                end else begin
                                    for (i = 0; i < 16; i = i + 1) begin
                                        if (i < num_top_ref)
                                            above_ref[i + 1] = 8'd127;
                                    end
                                end
                            end

                            if (need_left_ref) begin
                                if (has_left) begin
                                    for (i = 0; i < 16; i = i + 1) begin
                                        if (i < num_left_ref) begin
                                            if (i < blk_size)
                                                left_ref[i + 1] = left[i];
                                            else if (has_bottom_left)
                                                left_ref[i + 1] = bottom_left[i - blk_size];
                                            else
                                                left_ref[i + 1] = left[blk_size - 1];
                                        end
                                    end
                                end else if (has_top) begin
                                    for (i = 0; i < 16; i = i + 1) begin
                                        if (i < num_left_ref)
                                            left_ref[i + 1] = top[0];
                                    end
                                end else begin
                                    for (i = 0; i < 16; i = i + 1) begin
                                        if (i < num_left_ref)
                                            left_ref[i + 1] = 8'd129;
                                    end
                                end
                            end

                            if (need_above_left_ref) begin
                                if (has_top && has_left)
                                    above_ref[0] = top_left;
                                else if (has_top)
                                    above_ref[0] = top[0];
                                else if (has_left)
                                    above_ref[0] = left[0];
                                else
                                    above_ref[0] = 8'd128;
                                left_ref[0] = above_ref[0];
                            end
                        end

                        for (i = 0; i < 8; i = i + 1) begin
                            if (i < blk_size) begin
                                top_px = has_top ? top[i] :
                                         (has_left ? left[0] : 8'd127);
                                left_px = has_left ? left[row] :
                                          (has_top ? top[0] : 8'd129);

                                if (mode == V_PRED || mode == H_PRED || mode == D45_PRED ||
                                    mode == D135_PRED || mode == D113_PRED ||
                                    mode == D157_PRED || mode == D203_PRED ||
                                    mode == D67_PRED) begin
                                    if (p_angle < 9'd90) begin
                                        dir_x = ($signed({4'b0, row}) + 13'sd1) *
                                                $signed({2'b0, dx});
                                        base_x = ($signed(dir_x) >>> 6) + $signed({5'b0, i[3:0]});
                                        shift_amt = (dir_x >>> 1) & 6'h1F;
                                        if (base_x < max_base)
                                            pred[row * blk_size + i] <= interp_w32(
                                                above_ref[base_x + 1],
                                                above_ref[base_x + 2],
                                                shift_amt
                                            );
                                        else
                                            pred[row * blk_size + i] <= above_ref[max_base + 1];
                                    end else if (p_angle > 9'd90 && p_angle < 9'd180) begin
                                        dir_x = ($signed({5'b0, i[3:0]}) <<< 6) -
                                                (($signed({4'b0, row}) + 13'sd1) * $signed({2'b0, dx}));
                                        base_x = $signed(dir_x) >>> 6;
                                        if (base_x >= -1) begin
                                            shift_amt = (dir_x >>> 1) & 6'h1F;
                                            pred[row * blk_size + i] <= interp_w32(
                                                above_ref[base_x + 1],
                                                above_ref[base_x + 2],
                                                shift_amt
                                            );
                                        end else begin
                                            dir_y = ($signed({4'b0, row}) <<< 6) -
                                                    (($signed({5'b0, i[3:0]}) + 13'sd1) * $signed({2'b0, dy}));
                                            base_y = $signed(dir_y) >>> 6;
                                            shift_amt = (dir_y >>> 1) & 6'h1F;
                                            pred[row * blk_size + i] <= interp_w32(
                                                left_ref[base_y + 1],
                                                left_ref[base_y + 2],
                                                shift_amt
                                            );
                                        end
                                    end else if (p_angle > 9'd180) begin
                                        dir_y = (($signed({5'b0, i[3:0]}) + 13'sd1) * $signed({2'b0, dy}));
                                        base_y = ($signed(dir_y) >>> 6) + $signed({4'b0, row});
                                        shift_amt = (dir_y >>> 1) & 6'h1F;
                                        pred[row * blk_size + i] <= interp_w32(
                                            left_ref[base_y + 1],
                                            left_ref[base_y + 2],
                                            shift_amt
                                        );
                                    end else if (p_angle == 9'd90) begin
                                        pred[row * blk_size + i] <= above_ref[i + 1];
                                    end else begin
                                        pred[row * blk_size + i] <= left_ref[row + 1];
                                    end
                                end else case (mode)
                                    DC_PRED: begin
                                        pred[row * blk_size + i] <= dc_val;
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
