// av1_inverse_quant.v — AV1 Inverse Quantization
// dqcoeff = qcoeff * dequant
// Reference: AV1 spec section 7.12.3

module av1_inverse_quant (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    output reg         done,

    input  wire signed [15:0] qcoeff_in,
    input  wire        [15:0] dequant,

    output reg  signed [15:0] dqcoeff_out
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done       <= 0;
            dqcoeff_out <= 0;
        end else begin
            done <= 0;
            if (start) begin
                dqcoeff_out <= qcoeff_in * $signed({1'b0, dequant});
                done        <= 1;
            end
        end
    end

endmodule
