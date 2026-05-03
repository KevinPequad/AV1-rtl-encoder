// av1_chroma_residual.v — 4x4 chroma residual transform/quant/reconstruct core
//
// Standalone synthesis-friendly datapath for the next AV1 RTL encoder chroma
// checkpoint.  It consumes one 4x4 chroma source block plus its predictor,
// emits quantized 4x4 coefficients, and reconstructs the decoded chroma block.
// Top-level syntax integration remains separate so the byte path can be gated
// by decoder-compatible tests before this core is wired into frame emission.

module av1_chroma_residual (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire [7:0]  qindex,
    input  wire        dc_only,
    output reg         done,

    input  wire [7:0]  cur  [0:15],
    input  wire [7:0]  pred [0:15],

    output reg signed [15:0] qcoeff [0:15],
    output reg [7:0]  recon [0:15],
    output reg        block_has_coeff
);

    localparam [4:0]
        S_IDLE       = 5'd0,
        S_FWD_ROW    = 5'd1,
        S_FWD_ROWW   = 5'd2,
        S_FWD_COL    = 5'd3,
        S_FWD_COLW   = 5'd4,
        S_Q_START    = 5'd5,
        S_Q_WAIT     = 5'd6,
        S_IQ_START   = 5'd7,
        S_IQ_WAIT    = 5'd8,
        S_INV_ROW    = 5'd9,
        S_INV_ROWW   = 5'd10,
        S_INV_COL    = 5'd11,
        S_INV_COLW   = 5'd12,
        S_DONE       = 5'd13;

    reg [4:0] state;
    reg [2:0] row_idx;
    reg [2:0] col_idx;
    reg [4:0] proc_idx;
    integer i;

    reg signed [15:0] residual [0:15];
    reg signed [15:0] tx_tmp   [0:15];
    reg signed [15:0] coeff    [0:15];
    reg signed [15:0] dqcoeff  [0:15];
    reg signed [15:0] inv_tmp  [0:15];
    reg [15:0] dequant_dc;
    reg [15:0] dequant_ac;

    reg         xform_start;
    reg signed [15:0] xform_in [0:7];
    wire signed [15:0] xform_out [0:7];
    wire        xform_done;

    av1_transform u_transform (
        .clk(clk), .rst_n(rst_n),
        .start(xform_start),
        .is_4x4(1'b1),
        .done(xform_done),
        .in0(xform_in[0]), .in1(xform_in[1]), .in2(xform_in[2]), .in3(xform_in[3]),
        .in4(xform_in[4]), .in5(xform_in[5]), .in6(xform_in[6]), .in7(xform_in[7]),
        .out0(xform_out[0]), .out1(xform_out[1]), .out2(xform_out[2]), .out3(xform_out[3]),
        .out4(xform_out[4]), .out5(xform_out[5]), .out6(xform_out[6]), .out7(xform_out[7])
    );

    reg         quant_start;
    reg         quant_is_dc;
    reg signed [15:0] quant_coeff_in;
    wire signed [15:0] quant_coeff_out;
    wire [15:0] quant_dequant_out;
    wire        quant_done;

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

    reg signed [15:0] iq_qcoeff_in;
    reg [15:0] iq_dequant;
    wire signed [15:0] iq_dqcoeff_out;
    wire        iq_done;
    reg         iq_start;

    av1_inverse_quant u_inv_quant (
        .clk(clk), .rst_n(rst_n),
        .start(iq_start),
        .done(iq_done),
        .qcoeff_in(iq_qcoeff_in),
        .dequant(iq_dequant),
        .dqcoeff_out(iq_dqcoeff_out)
    );

    reg         inv_start;
    reg signed [15:0] inv_in [0:7];
    wire signed [15:0] inv_out [0:7];
    wire        inv_done;

    av1_inverse_transform u_inv_transform (
        .clk(clk), .rst_n(rst_n),
        .start(inv_start),
        .is_4x4(1'b1),
        .done(inv_done),
        .in0(inv_in[0]), .in1(inv_in[1]), .in2(inv_in[2]), .in3(inv_in[3]),
        .in4(inv_in[4]), .in5(inv_in[5]), .in6(inv_in[6]), .in7(inv_in[7]),
        .out0(inv_out[0]), .out1(inv_out[1]), .out2(inv_out[2]), .out3(inv_out[3]),
        .out4(inv_out[4]), .out5(inv_out[5]), .out6(inv_out[6]), .out7(inv_out[7])
    );

    function signed [15:0] round_shift16;
        input signed [15:0] val;
        input integer shift;
        reg signed [16:0] biased;
        begin
            if (shift <= 0) begin
                round_shift16 = val;
            end else begin
                biased = $signed({val[15], val}) + $signed(17'sd1 <<< (shift - 1));
                round_shift16 = biased >>> shift;
            end
        end
    endfunction

    function [7:0] clip_pred_res;
        input [7:0] pred_px;
        input signed [15:0] res_px;
        reg signed [16:0] sum;
        begin
            sum = $signed({1'b0, 8'b0, pred_px}) + res_px;
            if (sum < 0)
                clip_pred_res = 8'd0;
            else if (sum > 17'sd255)
                clip_pred_res = 8'd255;
            else
                clip_pred_res = sum[7:0];
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_IDLE;
            done <= 1'b0;
            row_idx <= 3'd0;
            col_idx <= 3'd0;
            proc_idx <= 5'd0;
            block_has_coeff <= 1'b0;
            dequant_dc <= 16'd0;
            dequant_ac <= 16'd0;
            xform_start <= 1'b0;
            quant_start <= 1'b0;
            iq_start <= 1'b0;
            inv_start <= 1'b0;
            for (i = 0; i < 16; i = i + 1) begin
                residual[i] <= 16'sd0;
                tx_tmp[i] <= 16'sd0;
                coeff[i] <= 16'sd0;
                dqcoeff[i] <= 16'sd0;
                inv_tmp[i] <= 16'sd0;
                qcoeff[i] <= 16'sd0;
                recon[i] <= 8'd128;
            end
            for (i = 0; i < 8; i = i + 1) begin
                xform_in[i] <= 16'sd0;
                inv_in[i] <= 16'sd0;
            end
        end else begin
            done <= 1'b0;
            xform_start <= 1'b0;
            quant_start <= 1'b0;
            iq_start <= 1'b0;
            inv_start <= 1'b0;

            case (state)
                S_IDLE: begin
                    if (start) begin
                        for (i = 0; i < 16; i = i + 1) begin
                            residual[i] <= $signed({1'b0, cur[i]}) - $signed({1'b0, pred[i]});
                            tx_tmp[i] <= 16'sd0;
                            coeff[i] <= 16'sd0;
                            dqcoeff[i] <= 16'sd0;
                            inv_tmp[i] <= 16'sd0;
                            qcoeff[i] <= 16'sd0;
                            recon[i] <= pred[i];
                        end
                        block_has_coeff <= 1'b0;
                        row_idx <= 3'd0;
                        state <= S_FWD_ROW;
                    end
                end

                S_FWD_ROW: begin
                    for (i = 0; i < 4; i = i + 1)
                        xform_in[i] <= residual[{row_idx[1:0], 2'b00} + i[1:0]];
                    for (i = 4; i < 8; i = i + 1)
                        xform_in[i] <= 16'sd0;
                    xform_start <= 1'b1;
                    state <= S_FWD_ROWW;
                end

                S_FWD_ROWW: begin
                    if (xform_done) begin
                        for (i = 0; i < 4; i = i + 1)
                            tx_tmp[{row_idx[1:0], 2'b00} + i[1:0]] <= xform_out[i];
                        if (row_idx < 3'd3) begin
                            row_idx <= row_idx + 1'b1;
                            state <= S_FWD_ROW;
                        end else begin
                            col_idx <= 3'd0;
                            state <= S_FWD_COL;
                        end
                    end
                end

                S_FWD_COL: begin
                    for (i = 0; i < 4; i = i + 1)
                        xform_in[i] <= tx_tmp[{i[1:0], col_idx[1:0]}];
                    for (i = 4; i < 8; i = i + 1)
                        xform_in[i] <= 16'sd0;
                    xform_start <= 1'b1;
                    state <= S_FWD_COLW;
                end

                S_FWD_COLW: begin
                    if (xform_done) begin
                        for (i = 0; i < 4; i = i + 1)
                            coeff[{i[1:0], col_idx[1:0]}] <= xform_out[i];
                        if (col_idx < 3'd3) begin
                            col_idx <= col_idx + 1'b1;
                            state <= S_FWD_COL;
                        end else begin
                            proc_idx <= 5'd0;
                            state <= S_Q_START;
                        end
                    end
                end

                S_Q_START: begin
                    quant_start <= 1'b1;
                    quant_is_dc <= (proc_idx == 5'd0);
                    quant_coeff_in <= coeff[proc_idx[3:0]];
                    state <= S_Q_WAIT;
                end

                S_Q_WAIT: begin
                    if (quant_done) begin
                        qcoeff[proc_idx[3:0]] <= (dc_only && proc_idx != 5'd0) ? 16'sd0 : quant_coeff_out;
                        if (((dc_only && proc_idx != 5'd0) ? 16'sd0 : quant_coeff_out) != 16'sd0)
                            block_has_coeff <= 1'b1;
                        if (proc_idx == 5'd0)
                            dequant_dc <= quant_dequant_out;
                        else if (proc_idx == 5'd1)
                            dequant_ac <= quant_dequant_out;
                        if (proc_idx < 5'd15) begin
                            proc_idx <= proc_idx + 1'b1;
                            state <= S_Q_START;
                        end else begin
                            proc_idx <= 5'd0;
                            state <= S_IQ_START;
                        end
                    end
                end

                S_IQ_START: begin
                    iq_start <= 1'b1;
                    iq_qcoeff_in <= qcoeff[proc_idx[3:0]];
                    iq_dequant <= (proc_idx == 5'd0) ? dequant_dc : dequant_ac;
                    state <= S_IQ_WAIT;
                end

                S_IQ_WAIT: begin
                    if (iq_done) begin
                        dqcoeff[proc_idx[3:0]] <= iq_dqcoeff_out;
                        if (proc_idx < 5'd15) begin
                            proc_idx <= proc_idx + 1'b1;
                            state <= S_IQ_START;
                        end else begin
                            row_idx <= 3'd0;
                            state <= S_INV_ROW;
                        end
                    end
                end

                S_INV_ROW: begin
                    for (i = 0; i < 4; i = i + 1)
                        inv_in[i] <= dqcoeff[{row_idx[1:0], 2'b00} + i[1:0]];
                    for (i = 4; i < 8; i = i + 1)
                        inv_in[i] <= 16'sd0;
                    inv_start <= 1'b1;
                    state <= S_INV_ROWW;
                end

                S_INV_ROWW: begin
                    if (inv_done) begin
                        for (i = 0; i < 4; i = i + 1)
                            inv_tmp[{row_idx[1:0], 2'b00} + i[1:0]] <= round_shift16(inv_out[i], 1);
                        if (row_idx < 3'd3) begin
                            row_idx <= row_idx + 1'b1;
                            state <= S_INV_ROW;
                        end else begin
                            col_idx <= 3'd0;
                            state <= S_INV_COL;
                        end
                    end
                end

                S_INV_COL: begin
                    for (i = 0; i < 4; i = i + 1)
                        inv_in[i] <= inv_tmp[{i[1:0], col_idx[1:0]}];
                    for (i = 4; i < 8; i = i + 1)
                        inv_in[i] <= 16'sd0;
                    inv_start <= 1'b1;
                    state <= S_INV_COLW;
                end

                S_INV_COLW: begin
                    if (inv_done) begin
                        for (i = 0; i < 4; i = i + 1)
                            recon[{i[1:0], col_idx[1:0]}] <=
                                clip_pred_res(pred[{i[1:0], col_idx[1:0]}], round_shift16(inv_out[i], 3));
                        if (col_idx < 3'd3) begin
                            col_idx <= col_idx + 1'b1;
                            state <= S_INV_COL;
                        end else begin
                            state <= S_DONE;
                        end
                    end
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
