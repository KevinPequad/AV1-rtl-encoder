// av1_chroma_inter_pred.v — 4:2:0 chroma inter predictor for 4x4 blocks
// Input MV is the luma q3 motion vector used by AV1 syntax.  For 4:2:0
// chroma, that same integer represents chroma q4 displacement:
//   chroma_samples = luma_mv_q3 / 16.
// This supports all 16 chroma phases with AV1 small-block regular filters.

module av1_chroma_inter_pred #(
    parameter CHROMA_WIDTH  = 640,
    parameter CHROMA_HEIGHT = 360
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,

    input  wire [10:0] cur_x,
    input  wire [10:0] cur_y,
    input  wire signed [15:0] mv_x_q3,
    input  wire signed [15:0] mv_y_q3,

    output reg  [17:0] ref_mem_addr,
    input  wire [7:0]  ref_mem_data,

    output reg         done,
    output wire [127:0] pred
);

    localparam [2:0]
        S_IDLE = 3'd0,
        S_ADDR = 3'd1,
        S_READ = 3'd2,
        S_DONE = 3'd3;

    localparam [1:0]
        MODE_FULL = 2'd0,
        MODE_H    = 2'd1,
        MODE_V    = 2'd2,
        MODE_HV   = 2'd3;

    reg [2:0] state;
    reg [1:0] mode;
    reg [3:0] out_idx;
    reg [2:0] tap_idx;
    reg [2:0] tap_y_idx;
    reg signed [15:0] base_x, base_y;
    reg [3:0] phase_x, phase_y;
    reg signed [31:0] h_acc, v_acc;
    reg signed [31:0] acc_next;
    reg signed [31:0] h_rounded;
    reg signed [31:0] v_next;
    integer i;
    reg [7:0] pred_mem [0:15];

    assign pred = {
        pred_mem[15], pred_mem[14], pred_mem[13], pred_mem[12],
        pred_mem[11], pred_mem[10], pred_mem[9],  pred_mem[8],
        pred_mem[7],  pred_mem[6],  pred_mem[5],  pred_mem[4],
        pred_mem[3],  pred_mem[2],  pred_mem[1],  pred_mem[0]
    };

    function integer clamp_i;
        input integer v;
        input integer lo;
        input integer hi;
        begin
            if (v < lo) clamp_i = lo;
            else if (v > hi) clamp_i = hi;
            else clamp_i = v;
        end
    endfunction

    function [17:0] ref_addr_clamped;
        input signed [15:0] sx;
        input signed [15:0] sy;
        integer cx;
        integer cy;
        begin
            cx = clamp_i(sx, 0, CHROMA_WIDTH - 1);
            cy = clamp_i(sy, 0, CHROMA_HEIGHT - 1);
            ref_addr_clamped = cy * CHROMA_WIDTH + cx;
        end
    endfunction

    // Small-block AV1 regular filters for width/height <= 4, scaled to sum 128.
    // Phase 0 is the identity. Phases 1..15 are dav1d's small regular filters
    // doubled from sum-64 to sum-128 to match the luma predictor convention.
    function signed [8:0] small_regular_coeff;
        input [3:0] phase;
        input [2:0] tap;
        reg [6:0] idx;
        begin
            idx = {phase, tap};
            case (idx)
                7'd0: small_regular_coeff = 9'sd0;
                7'd1: small_regular_coeff = 9'sd0;
                7'd2: small_regular_coeff = 9'sd0;
                7'd3: small_regular_coeff = 9'sd128;
                7'd4: small_regular_coeff = 9'sd0;
                7'd5: small_regular_coeff = 9'sd0;
                7'd6: small_regular_coeff = 9'sd0;
                7'd7: small_regular_coeff = 9'sd0;

                7'd8: small_regular_coeff = 9'sd0;
                7'd9: small_regular_coeff = 9'sd0;
                7'd10: small_regular_coeff = -9'sd4;
                7'd11: small_regular_coeff = 9'sd126;
                7'd12: small_regular_coeff = 9'sd8;
                7'd13: small_regular_coeff = -9'sd2;
                7'd14: small_regular_coeff = 9'sd0;
                7'd15: small_regular_coeff = 9'sd0;

                7'd16: small_regular_coeff = 9'sd0;
                7'd17: small_regular_coeff = 9'sd0;
                7'd18: small_regular_coeff = -9'sd8;
                7'd19: small_regular_coeff = 9'sd122;
                7'd20: small_regular_coeff = 9'sd18;
                7'd21: small_regular_coeff = -9'sd4;
                7'd22: small_regular_coeff = 9'sd0;
                7'd23: small_regular_coeff = 9'sd0;

                7'd24: small_regular_coeff = 9'sd0;
                7'd25: small_regular_coeff = 9'sd0;
                7'd26: small_regular_coeff = -9'sd10;
                7'd27: small_regular_coeff = 9'sd116;
                7'd28: small_regular_coeff = 9'sd28;
                7'd29: small_regular_coeff = -9'sd6;
                7'd30: small_regular_coeff = 9'sd0;
                7'd31: small_regular_coeff = 9'sd0;

                7'd32: small_regular_coeff = 9'sd0;
                7'd33: small_regular_coeff = 9'sd0;
                7'd34: small_regular_coeff = -9'sd12;
                7'd35: small_regular_coeff = 9'sd110;
                7'd36: small_regular_coeff = 9'sd38;
                7'd37: small_regular_coeff = -9'sd8;
                7'd38: small_regular_coeff = 9'sd0;
                7'd39: small_regular_coeff = 9'sd0;

                7'd40: small_regular_coeff = 9'sd0;
                7'd41: small_regular_coeff = 9'sd0;
                7'd42: small_regular_coeff = -9'sd12;
                7'd43: small_regular_coeff = 9'sd102;
                7'd44: small_regular_coeff = 9'sd48;
                7'd45: small_regular_coeff = -9'sd10;
                7'd46: small_regular_coeff = 9'sd0;
                7'd47: small_regular_coeff = 9'sd0;

                7'd48: small_regular_coeff = 9'sd0;
                7'd49: small_regular_coeff = 9'sd0;
                7'd50: small_regular_coeff = -9'sd14;
                7'd51: small_regular_coeff = 9'sd94;
                7'd52: small_regular_coeff = 9'sd58;
                7'd53: small_regular_coeff = -9'sd10;
                7'd54: small_regular_coeff = 9'sd0;
                7'd55: small_regular_coeff = 9'sd0;

                7'd56: small_regular_coeff = 9'sd0;
                7'd57: small_regular_coeff = 9'sd0;
                7'd58: small_regular_coeff = -9'sd12;
                7'd59: small_regular_coeff = 9'sd84;
                7'd60: small_regular_coeff = 9'sd66;
                7'd61: small_regular_coeff = -9'sd10;
                7'd62: small_regular_coeff = 9'sd0;
                7'd63: small_regular_coeff = 9'sd0;

                7'd64: small_regular_coeff = 9'sd0;
                7'd65: small_regular_coeff = 9'sd0;
                7'd66: small_regular_coeff = -9'sd12;
                7'd67: small_regular_coeff = 9'sd76;
                7'd68: small_regular_coeff = 9'sd76;
                7'd69: small_regular_coeff = -9'sd12;
                7'd70: small_regular_coeff = 9'sd0;
                7'd71: small_regular_coeff = 9'sd0;

                7'd72: small_regular_coeff = 9'sd0;
                7'd73: small_regular_coeff = 9'sd0;
                7'd74: small_regular_coeff = -9'sd10;
                7'd75: small_regular_coeff = 9'sd66;
                7'd76: small_regular_coeff = 9'sd84;
                7'd77: small_regular_coeff = -9'sd12;
                7'd78: small_regular_coeff = 9'sd0;
                7'd79: small_regular_coeff = 9'sd0;

                7'd80: small_regular_coeff = 9'sd0;
                7'd81: small_regular_coeff = 9'sd0;
                7'd82: small_regular_coeff = -9'sd10;
                7'd83: small_regular_coeff = 9'sd58;
                7'd84: small_regular_coeff = 9'sd94;
                7'd85: small_regular_coeff = -9'sd14;
                7'd86: small_regular_coeff = 9'sd0;
                7'd87: small_regular_coeff = 9'sd0;

                7'd88: small_regular_coeff = 9'sd0;
                7'd89: small_regular_coeff = 9'sd0;
                7'd90: small_regular_coeff = -9'sd10;
                7'd91: small_regular_coeff = 9'sd48;
                7'd92: small_regular_coeff = 9'sd102;
                7'd93: small_regular_coeff = -9'sd12;
                7'd94: small_regular_coeff = 9'sd0;
                7'd95: small_regular_coeff = 9'sd0;

                7'd96: small_regular_coeff = 9'sd0;
                7'd97: small_regular_coeff = 9'sd0;
                7'd98: small_regular_coeff = -9'sd8;
                7'd99: small_regular_coeff = 9'sd38;
                7'd100: small_regular_coeff = 9'sd110;
                7'd101: small_regular_coeff = -9'sd12;
                7'd102: small_regular_coeff = 9'sd0;
                7'd103: small_regular_coeff = 9'sd0;

                7'd104: small_regular_coeff = 9'sd0;
                7'd105: small_regular_coeff = 9'sd0;
                7'd106: small_regular_coeff = -9'sd6;
                7'd107: small_regular_coeff = 9'sd28;
                7'd108: small_regular_coeff = 9'sd116;
                7'd109: small_regular_coeff = -9'sd10;
                7'd110: small_regular_coeff = 9'sd0;
                7'd111: small_regular_coeff = 9'sd0;

                7'd112: small_regular_coeff = 9'sd0;
                7'd113: small_regular_coeff = 9'sd0;
                7'd114: small_regular_coeff = -9'sd4;
                7'd115: small_regular_coeff = 9'sd18;
                7'd116: small_regular_coeff = 9'sd122;
                7'd117: small_regular_coeff = -9'sd8;
                7'd118: small_regular_coeff = 9'sd0;
                7'd119: small_regular_coeff = 9'sd0;

                7'd120: small_regular_coeff = 9'sd0;
                7'd121: small_regular_coeff = 9'sd0;
                7'd122: small_regular_coeff = -9'sd2;
                7'd123: small_regular_coeff = 9'sd8;
                7'd124: small_regular_coeff = 9'sd126;
                7'd125: small_regular_coeff = -9'sd4;
                7'd126: small_regular_coeff = 9'sd0;
                7'd127: small_regular_coeff = 9'sd0;
                default: small_regular_coeff = 9'sd0;
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

    // AV1 inter prediction always performs horizontal rounding first, then
    // vertical rounding. In horizontal-only mode the vertical phase-0 pass is
    // not algebraically equivalent to a single (sum + 64) >> 7 round because
    // the horizontal intermediate is rounded by InterRound0=3 first.
    function [7:0] clip_round_filter_h_only;
        input signed [31:0] sum;
        reg signed [31:0] rounded;
        begin
            rounded = (round_hv_horizontal(sum) + 32'sd8) >>> 4;
            if (rounded < 0)
                clip_round_filter_h_only = 8'd0;
            else if (rounded > 255)
                clip_round_filter_h_only = 8'd255;
            else
                clip_round_filter_h_only = rounded[7:0];
        end
    endfunction

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
            if (out_idx == 4'd15) begin
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
            ref_mem_addr <= 18'd0;
            out_idx <= 4'd0;
            tap_idx <= 3'd0;
            tap_y_idx <= 3'd0;
            base_x <= 16'sd0;
            base_y <= 16'sd0;
            phase_x <= 4'd0;
            phase_y <= 4'd0;
            mode <= MODE_FULL;
            h_acc <= 32'sd0;
            v_acc <= 32'sd0;
            for (i = 0; i < 16; i = i + 1)
                pred_mem[i] <= 8'd128;
        end else begin
            done <= 1'b0;
            case (state)
                S_IDLE: begin
                    if (start) begin
                        out_idx <= 4'd0;
                        tap_idx <= 3'd0;
                        tap_y_idx <= 3'd0;
                        h_acc <= 32'sd0;
                        v_acc <= 32'sd0;
                        base_x <= $signed({5'd0, cur_x}) + (mv_x_q3 >>> 4);
                        base_y <= $signed({5'd0, cur_y}) + (mv_y_q3 >>> 4);
                        phase_x <= mv_x_q3[3:0];
                        phase_y <= mv_y_q3[3:0];
                        if (mv_x_q3[3:0] == 4'd0 && mv_y_q3[3:0] == 4'd0)
                            mode <= MODE_FULL;
                        else if (mv_y_q3[3:0] == 4'd0)
                            mode <= MODE_H;
                        else if (mv_x_q3[3:0] == 4'd0)
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
                                base_x + $signed({12'd0, out_idx[1:0]}),
                                base_y + $signed({12'd0, out_idx[3:2]}));
                        end
                        MODE_H: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({12'd0, out_idx[1:0]}) + $signed({13'd0, tap_idx}) - 16'sd3,
                                base_y + $signed({12'd0, out_idx[3:2]}));
                        end
                        MODE_V: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({12'd0, out_idx[1:0]}),
                                base_y + $signed({12'd0, out_idx[3:2]}) + $signed({13'd0, tap_idx}) - 16'sd3);
                        end
                        default: begin
                            ref_mem_addr <= ref_addr_clamped(
                                base_x + $signed({12'd0, out_idx[1:0]}) + $signed({13'd0, tap_idx}) - 16'sd3,
                                base_y + $signed({12'd0, out_idx[3:2]}) + $signed({13'd0, tap_y_idx}) - 16'sd3);
                        end
                    endcase
                    state <= S_READ;
                end

                S_READ: begin
                    case (mode)
                        MODE_FULL: begin
                            pred_mem[out_idx] <= ref_mem_data;
                            advance_pixel();
                        end
                        MODE_H: begin
                            acc_next = h_acc + small_regular_coeff(phase_x, tap_idx) * $signed({1'b0, ref_mem_data});
                            if (tap_idx == 3'd7) begin
                                pred_mem[out_idx] <= clip_round_filter_h_only(acc_next);
                                h_acc <= 32'sd0;
                                advance_pixel();
                            end else begin
                                h_acc <= acc_next;
                                tap_idx <= tap_idx + 1'b1;
                                state <= S_ADDR;
                            end
                        end
                        MODE_V: begin
                            acc_next = v_acc + small_regular_coeff(phase_y, tap_idx) * $signed({1'b0, ref_mem_data});
                            if (tap_idx == 3'd7) begin
                                pred_mem[out_idx] <= clip_round_filter(acc_next);
                                v_acc <= 32'sd0;
                                advance_pixel();
                            end else begin
                                v_acc <= acc_next;
                                tap_idx <= tap_idx + 1'b1;
                                state <= S_ADDR;
                            end
                        end
                        default: begin
                            acc_next = h_acc + small_regular_coeff(phase_x, tap_idx) * $signed({1'b0, ref_mem_data});
                            if (tap_idx == 3'd7) begin
                                h_rounded = round_hv_horizontal(acc_next);
                                v_next = v_acc + small_regular_coeff(phase_y, tap_y_idx) * h_rounded;
                                h_acc <= 32'sd0;
                                if (tap_y_idx == 3'd7) begin
                                    pred_mem[out_idx] <= clip_round_filter_hv(v_next);
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
