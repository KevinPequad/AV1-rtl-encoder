// av1_inter_pred.v -- 8x8 LAST-frame inter predictor
//
// ASIC-oriented reference-frame predictor for the reduced AV1 bring-up path.
// Motion vectors are q3 luma units (1/8-pel).  The single-axis fractional
// paths use AV1's regular 8-tap filter phases mapped from q3 -> q4 by
// phase = frac_q3 << 1.  The 2D path is separable and intended as the next
// integration point after standalone validation.

module av1_inter_pred #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720
) (
    input  wire clk,
    input  wire rst_n,
    input  wire start,

    input  wire [10:0] cur_x,
    input  wire [10:0] cur_y,
    input  wire signed [15:0] mv_x_q3,
    input  wire signed [15:0] mv_y_q3,

    output reg  [19:0] ref_mem_addr,
    input  wire [7:0]  ref_mem_data,

    output reg         done,
    output reg [7:0]   pred [0:63]
);

    localparam S_IDLE = 3'd0;
    localparam S_ADDR = 3'd1;
    localparam S_READ = 3'd2;
    localparam S_DONE = 3'd3;

    localparam MODE_FULL = 2'd0;
    localparam MODE_H    = 2'd1;
    localparam MODE_V    = 2'd2;
    localparam MODE_HV   = 2'd3;

    reg [2:0] state;
    reg [1:0] mode;
    reg [5:0] out_idx;
    reg [2:0] tap_idx;
    reg [2:0] tap_y_idx;
    reg signed [15:0] base_x;
    reg signed [15:0] base_y;
    reg [3:0] phase_x;
    reg [3:0] phase_y;
    reg signed [31:0] h_acc;
    reg signed [31:0] v_acc;

    integer i;
    reg signed [31:0] acc_next;
    reg signed [31:0] h_rounded;
    reg signed [31:0] v_next;

    function integer clamp_i;
        input integer val;
        input integer lo;
        input integer hi;
        begin
            if (val < lo)
                clamp_i = lo;
            else if (val > hi)
                clamp_i = hi;
            else
                clamp_i = val;
        end
    endfunction

    function [19:0] ref_addr_clamped;
        input signed [15:0] sx;
        input signed [15:0] sy;
        integer cx;
        integer cy;
        begin
            cx = clamp_i(sx, 0, FRAME_WIDTH - 1);
            cy = clamp_i(sy, 0, FRAME_HEIGHT - 1);
            ref_addr_clamped = cy * FRAME_WIDTH + cx;
        end
    endfunction

    function signed [8:0] regular_coeff;
        input [3:0] phase;
        input [2:0] tap;
        begin
            case ({phase, tap})
                7'd0: regular_coeff = 9'sd0;
                7'd1: regular_coeff = 9'sd0;
                7'd2: regular_coeff = 9'sd0;
                7'd3: regular_coeff = 9'sd128;
                7'd4: regular_coeff = 9'sd0;
                7'd5: regular_coeff = 9'sd0;
                7'd6: regular_coeff = 9'sd0;
                7'd7: regular_coeff = 9'sd0;
                7'd8: regular_coeff = 9'sd0;
                7'd9: regular_coeff = 9'sd2;
                7'd10: regular_coeff = -9'sd6;
                7'd11: regular_coeff = 9'sd126;
                7'd12: regular_coeff = 9'sd8;
                7'd13: regular_coeff = -9'sd2;
                7'd14: regular_coeff = 9'sd0;
                7'd15: regular_coeff = 9'sd0;
                7'd16: regular_coeff = 9'sd0;
                7'd17: regular_coeff = 9'sd2;
                7'd18: regular_coeff = -9'sd10;
                7'd19: regular_coeff = 9'sd122;
                7'd20: regular_coeff = 9'sd18;
                7'd21: regular_coeff = -9'sd4;
                7'd22: regular_coeff = 9'sd0;
                7'd23: regular_coeff = 9'sd0;
                7'd24: regular_coeff = 9'sd0;
                7'd25: regular_coeff = 9'sd2;
                7'd26: regular_coeff = -9'sd12;
                7'd27: regular_coeff = 9'sd116;
                7'd28: regular_coeff = 9'sd28;
                7'd29: regular_coeff = -9'sd8;
                7'd30: regular_coeff = 9'sd2;
                7'd31: regular_coeff = 9'sd0;
                7'd32: regular_coeff = 9'sd0;
                7'd33: regular_coeff = 9'sd2;
                7'd34: regular_coeff = -9'sd14;
                7'd35: regular_coeff = 9'sd110;
                7'd36: regular_coeff = 9'sd38;
                7'd37: regular_coeff = -9'sd10;
                7'd38: regular_coeff = 9'sd2;
                7'd39: regular_coeff = 9'sd0;
                7'd40: regular_coeff = 9'sd0;
                7'd41: regular_coeff = 9'sd2;
                7'd42: regular_coeff = -9'sd14;
                7'd43: regular_coeff = 9'sd102;
                7'd44: regular_coeff = 9'sd48;
                7'd45: regular_coeff = -9'sd12;
                7'd46: regular_coeff = 9'sd2;
                7'd47: regular_coeff = 9'sd0;
                7'd48: regular_coeff = 9'sd0;
                7'd49: regular_coeff = 9'sd2;
                7'd50: regular_coeff = -9'sd16;
                7'd51: regular_coeff = 9'sd94;
                7'd52: regular_coeff = 9'sd58;
                7'd53: regular_coeff = -9'sd12;
                7'd54: regular_coeff = 9'sd2;
                7'd55: regular_coeff = 9'sd0;
                7'd56: regular_coeff = 9'sd0;
                7'd57: regular_coeff = 9'sd2;
                7'd58: regular_coeff = -9'sd14;
                7'd59: regular_coeff = 9'sd84;
                7'd60: regular_coeff = 9'sd66;
                7'd61: regular_coeff = -9'sd12;
                7'd62: regular_coeff = 9'sd2;
                7'd63: regular_coeff = 9'sd0;
                7'd64: regular_coeff = 9'sd0;
                7'd65: regular_coeff = 9'sd2;
                7'd66: regular_coeff = -9'sd14;
                7'd67: regular_coeff = 9'sd76;
                7'd68: regular_coeff = 9'sd76;
                7'd69: regular_coeff = -9'sd14;
                7'd70: regular_coeff = 9'sd2;
                7'd71: regular_coeff = 9'sd0;
                7'd72: regular_coeff = 9'sd0;
                7'd73: regular_coeff = 9'sd2;
                7'd74: regular_coeff = -9'sd12;
                7'd75: regular_coeff = 9'sd66;
                7'd76: regular_coeff = 9'sd84;
                7'd77: regular_coeff = -9'sd14;
                7'd78: regular_coeff = 9'sd2;
                7'd79: regular_coeff = 9'sd0;
                7'd80: regular_coeff = 9'sd0;
                7'd81: regular_coeff = 9'sd2;
                7'd82: regular_coeff = -9'sd12;
                7'd83: regular_coeff = 9'sd58;
                7'd84: regular_coeff = 9'sd94;
                7'd85: regular_coeff = -9'sd16;
                7'd86: regular_coeff = 9'sd2;
                7'd87: regular_coeff = 9'sd0;
                7'd88: regular_coeff = 9'sd0;
                7'd89: regular_coeff = 9'sd2;
                7'd90: regular_coeff = -9'sd12;
                7'd91: regular_coeff = 9'sd48;
                7'd92: regular_coeff = 9'sd102;
                7'd93: regular_coeff = -9'sd14;
                7'd94: regular_coeff = 9'sd2;
                7'd95: regular_coeff = 9'sd0;
                7'd96: regular_coeff = 9'sd0;
                7'd97: regular_coeff = 9'sd2;
                7'd98: regular_coeff = -9'sd10;
                7'd99: regular_coeff = 9'sd38;
                7'd100: regular_coeff = 9'sd110;
                7'd101: regular_coeff = -9'sd14;
                7'd102: regular_coeff = 9'sd2;
                7'd103: regular_coeff = 9'sd0;
                7'd104: regular_coeff = 9'sd0;
                7'd105: regular_coeff = 9'sd2;
                7'd106: regular_coeff = -9'sd8;
                7'd107: regular_coeff = 9'sd28;
                7'd108: regular_coeff = 9'sd116;
                7'd109: regular_coeff = -9'sd12;
                7'd110: regular_coeff = 9'sd2;
                7'd111: regular_coeff = 9'sd0;
                7'd112: regular_coeff = 9'sd0;
                7'd113: regular_coeff = 9'sd0;
                7'd114: regular_coeff = -9'sd4;
                7'd115: regular_coeff = 9'sd18;
                7'd116: regular_coeff = 9'sd122;
                7'd117: regular_coeff = -9'sd10;
                7'd118: regular_coeff = 9'sd2;
                7'd119: regular_coeff = 9'sd0;
                7'd120: regular_coeff = 9'sd0;
                7'd121: regular_coeff = 9'sd0;
                7'd122: regular_coeff = -9'sd2;
                7'd123: regular_coeff = 9'sd8;
                7'd124: regular_coeff = 9'sd126;
                7'd125: regular_coeff = -9'sd6;
                7'd126: regular_coeff = 9'sd2;
                7'd127: regular_coeff = 9'sd0;
                default: regular_coeff = 9'sd0;
            endcase
        end
    endfunction

    function [7:0] clip_round_filter;
        input signed [31:0] sum;
        reg signed [31:0] rounded;
        begin
            rounded = (sum + 32'sd64) >>> 7;
            if (rounded < 0)
                clip_round_filter = 8'd0;
            else if (rounded > 255)
                clip_round_filter = 8'd255;
            else
                clip_round_filter = rounded[7:0];
        end
    endfunction

    function signed [31:0] round_filter_signed;
        input signed [31:0] sum;
        begin
            round_filter_signed = (sum + 32'sd64) >>> 7;
        end
    endfunction

    // AV1 two-dimensional 8-tap prediction keeps 4 intermediate bits after
    // the horizontal pass for 8-bit content.  Coefficients in this RTL are the
    // spec/dav1d coefficients scaled by 2 (sum 128 instead of 64), so the
    // horizontal stage rounds by 3 and the vertical/output stage rounds by 11.
    function signed [31:0] round_hv_horizontal;
        input signed [31:0] sum;
        begin
            round_hv_horizontal = (sum + 32'sd4) >>> 3;
        end
    endfunction

    function [7:0] clip_round_filter_hv;
        input signed [31:0] sum;
        reg signed [31:0] rounded;
        begin
            rounded = (sum + 32'sd1024) >>> 11;
            if (rounded < 0)
                clip_round_filter_hv = 8'd0;
            else if (rounded > 255)
                clip_round_filter_hv = 8'd255;
            else
                clip_round_filter_hv = rounded[7:0];
        end
    endfunction

    task advance_pixel;
        begin
            if (out_idx == 6'd63) begin
                state <= S_DONE;
            end else begin
                out_idx <= out_idx + 1'b1;
                tap_idx <= 3'd0;
                tap_y_idx <= 3'd0;
                h_acc <= 32'sd0;
                v_acc <= 32'sd0;
                state <= S_ADDR;
            end
        end
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_IDLE;
            done <= 1'b0;
            ref_mem_addr <= 20'd0;
            out_idx <= 6'd0;
            tap_idx <= 3'd0;
            tap_y_idx <= 3'd0;
            base_x <= 16'sd0;
            base_y <= 16'sd0;
            phase_x <= 4'd0;
            phase_y <= 4'd0;
            mode <= MODE_FULL;
            h_acc <= 32'sd0;
            v_acc <= 32'sd0;
            for (i = 0; i < 64; i = i + 1)
                pred[i] <= 8'd0;
        end else begin
            done <= 1'b0;
            case (state)
                S_IDLE: begin
                    if (start) begin
                        out_idx <= 6'd0;
                        tap_idx <= 3'd0;
                        tap_y_idx <= 3'd0;
                        h_acc <= 32'sd0;
                        v_acc <= 32'sd0;
                        base_x <= $signed({5'd0, cur_x}) + (mv_x_q3 >>> 3);
                        base_y <= $signed({5'd0, cur_y}) + (mv_y_q3 >>> 3);
                        phase_x <= {mv_x_q3[2:0], 1'b0};
                        phase_y <= {mv_y_q3[2:0], 1'b0};
                        if (mv_x_q3[2:0] == 3'd0 && mv_y_q3[2:0] == 3'd0)
                            mode <= MODE_FULL;
                        else if (mv_y_q3[2:0] == 3'd0)
                            mode <= MODE_H;
                        else if (mv_x_q3[2:0] == 3'd0)
                            mode <= MODE_V;
                        else
                            mode <= MODE_HV;
                        state <= S_ADDR;
                    end
                end

                S_ADDR: begin
                    case (mode)
                        MODE_FULL: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({10'd0, out_idx[2:0]}),
                                base_y + $signed({10'd0, out_idx[5:3]}));
                        end
                        MODE_H: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({10'd0, out_idx[2:0]}) + $signed({13'd0, tap_idx}) - 16'sd3,
                                base_y + $signed({10'd0, out_idx[5:3]}));
                        end
                        MODE_V: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({10'd0, out_idx[2:0]}),
                                base_y + $signed({10'd0, out_idx[5:3]}) + $signed({13'd0, tap_idx}) - 16'sd3);
                        end
                        default: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({10'd0, out_idx[2:0]}) + $signed({13'd0, tap_idx}) - 16'sd3,
                                base_y + $signed({10'd0, out_idx[5:3]}) + $signed({13'd0, tap_y_idx}) - 16'sd3);
                        end
                    endcase
                    state <= S_READ;
                end

                S_READ: begin
                    case (mode)
                        MODE_FULL: begin
                            pred[out_idx] <= ref_mem_data;
                            advance_pixel();
                        end
                        MODE_H: begin
                            acc_next = h_acc + regular_coeff(phase_x, tap_idx) * $signed({1'b0, ref_mem_data});
                            if (tap_idx == 3'd7) begin
                                pred[out_idx] <= clip_round_filter(acc_next);
                                h_acc <= 32'sd0;
                                advance_pixel();
                            end else begin
                                h_acc <= acc_next;
                                tap_idx <= tap_idx + 1'b1;
                                state <= S_ADDR;
                            end
                        end
                        MODE_V: begin
                            acc_next = v_acc + regular_coeff(phase_y, tap_idx) * $signed({1'b0, ref_mem_data});
                            if (tap_idx == 3'd7) begin
                                pred[out_idx] <= clip_round_filter(acc_next);
                                v_acc <= 32'sd0;
                                advance_pixel();
                            end else begin
                                v_acc <= acc_next;
                                tap_idx <= tap_idx + 1'b1;
                                state <= S_ADDR;
                            end
                        end
                        default: begin
                            acc_next = h_acc + regular_coeff(phase_x, tap_idx) * $signed({1'b0, ref_mem_data});
                            if (tap_idx == 3'd7) begin
                                h_rounded = round_hv_horizontal(acc_next);
                                v_next = v_acc + regular_coeff(phase_y, tap_y_idx) * h_rounded;
                                h_acc <= 32'sd0;
                                if (tap_y_idx == 3'd7) begin
                                    pred[out_idx] <= clip_round_filter_hv(v_next);
                                    v_acc <= 32'sd0;
                                    advance_pixel();
                                end else begin
                                    v_acc <= v_next;
                                    tap_y_idx <= tap_y_idx + 1'b1;
                                    tap_idx <= 3'd0;
                                    state <= S_ADDR;
                                end
                            end else begin
                                h_acc <= acc_next;
                                tap_idx <= tap_idx + 1'b1;
                                state <= S_ADDR;
                            end
                        end
                    endcase
                end

                S_DONE: begin
                    done <= 1'b1;
                    state <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
