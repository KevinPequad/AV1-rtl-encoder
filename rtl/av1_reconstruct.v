// av1_reconstruct.v — Pixel Reconstruction
// recon = clamp(prediction + residual, 0, 255)
// Used after inverse quantization + inverse transform.

module av1_reconstruct (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    output reg         done,

    // Prediction pixel (8-bit unsigned)
    input  wire [7:0]  pred_in,
    // Residual from inverse transform (16-bit signed)
    input  wire signed [15:0] residual_in,

    // Reconstructed pixel (8-bit unsigned, clamped)
    output reg  [7:0]  recon_out
);

    reg signed [16:0] sum;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done      <= 0;
            recon_out <= 0;
        end else begin
            done <= 0;
            if (start) begin
                sum = $signed({1'b0, 8'b0, pred_in}) + residual_in;
                if (sum < 0)
                    recon_out <= 8'd0;
                else if (sum > 255)
                    recon_out <= 8'd255;
                else
                    recon_out <= sum[7:0];
                done <= 1;
            end
        end
    end

endmodule
