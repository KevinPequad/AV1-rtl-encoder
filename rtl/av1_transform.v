// av1_transform.v — AV1 Forward DCT-II Transform (4x4 and 8x8)
// Constants derived from SVT-AV1 cospi_arr_data with cos_bit=13
// cospi[k] = round(cos(k * pi / 128) * 8192)
//
// Reference: SVT-AV1/Source/Lib/Codec/transforms.c
//   svt_av1_fdct4_new() and svt_av1_fdct8_new()

module av1_transform (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        is_4x4,       // 1 = 4x4 transform, 0 = 8x8 transform
    output reg         done,

    // Input: row of pixels (up to 8 values, 16-bit signed)
    input  wire signed [15:0] in0, in1, in2, in3,
    input  wire signed [15:0] in4, in5, in6, in7,

    // Output: transform coefficients (16-bit signed)
    output reg  signed [15:0] out0, out1, out2, out3,
    output reg  signed [15:0] out4, out5, out6, out7
);

    // cos_bit = 13 for forward transforms (4x4 and 8x8)
    localparam COS_BIT = 13;

    // cospi values at cos_bit=13 from SVT-AV1 cospi_arr_data[3]
    // cospi[k] = round(cos(k * pi / 128) * 8192)
    localparam signed [15:0] COSPI_8  = 16'sd8035;
    localparam signed [15:0] COSPI_16 = 16'sd7568;
    localparam signed [15:0] COSPI_24 = 16'sd6811;
    localparam signed [15:0] COSPI_32 = 16'sd5793;
    localparam signed [15:0] COSPI_40 = 16'sd4551;
    localparam signed [15:0] COSPI_48 = 16'sd3135;
    localparam signed [15:0] COSPI_56 = 16'sd1598;

    // half_btf: round_shift(w0*in0 + w1*in1, cos_bit)
    function signed [15:0] half_btf;
        input signed [15:0] w0;
        input signed [15:0] a;
        input signed [15:0] w1;
        input signed [15:0] b;
        reg signed [31:0] prod;
        begin
            prod = w0 * a + w1 * b;
            half_btf = (prod + (1 <<< (COS_BIT - 1))) >>> COS_BIT;
        end
    endfunction

    // Pipeline stages
    reg [2:0] stage;
    reg signed [15:0] bf [0:7]; // butterfly intermediate
    reg signed [15:0] st [0:7]; // step intermediate

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done  <= 0;
            stage <= 0;
            out0 <= 0; out1 <= 0; out2 <= 0; out3 <= 0;
            out4 <= 0; out5 <= 0; out6 <= 0; out7 <= 0;
        end else begin
            done <= 0;

            if (start) begin
                stage <= 1;
                if (is_4x4) begin
                    // 4-point DCT stage 1: butterflies
                    bf[0] <= in0 + in3;
                    bf[1] <= in1 + in2;
                    bf[2] <= in1 - in2;  // -in2 + in1
                    bf[3] <= in0 - in3;  // -in3 + in0
                end else begin
                    // 8-point DCT stage 1: butterflies
                    bf[0] <= in0 + in7;
                    bf[1] <= in1 + in6;
                    bf[2] <= in2 + in5;
                    bf[3] <= in3 + in4;
                    bf[4] <= in3 - in4;
                    bf[5] <= in2 - in5;
                    bf[6] <= in1 - in6;
                    bf[7] <= in0 - in7;
                end
            end else if (stage == 3'd1) begin
                stage <= 2;
                if (is_4x4) begin
                    // 4-point DCT stage 2: cosine multiplies
                    st[0] <= half_btf( COSPI_32, bf[0],  COSPI_32, bf[1]);
                    st[1] <= half_btf(-COSPI_32, bf[1],  COSPI_32, bf[0]);
                    st[2] <= half_btf( COSPI_48, bf[2],  COSPI_16, bf[3]);
                    st[3] <= half_btf( COSPI_48, bf[3], -COSPI_16, bf[2]);
                end else begin
                    // 8-point DCT stage 2: partial butterflies + rotation
                    st[0] <= bf[0] + bf[3];
                    st[1] <= bf[1] + bf[2];
                    st[2] <= bf[1] - bf[2];
                    st[3] <= bf[0] - bf[3];
                    st[4] <= bf[4];
                    st[5] <= half_btf(-COSPI_32, bf[5], COSPI_32, bf[6]);
                    st[6] <= half_btf( COSPI_32, bf[6], COSPI_32, bf[5]);
                    st[7] <= bf[7];
                end
            end else if (stage == 3'd2) begin
                stage <= 3;
                if (is_4x4) begin
                    // 4-point DCT stage 3: output permutation
                    out0 <= st[0];  // DC
                    out1 <= st[2];
                    out2 <= st[1];
                    out3 <= st[3];
                    done  <= 1;
                    stage <= 0;
                end else begin
                    // 8-point DCT stage 3: more butterflies
                    bf[0] <= half_btf( COSPI_32, st[0],  COSPI_32, st[1]);
                    bf[1] <= half_btf(-COSPI_32, st[1],  COSPI_32, st[0]);
                    bf[2] <= half_btf( COSPI_48, st[2],  COSPI_16, st[3]);
                    bf[3] <= half_btf( COSPI_48, st[3], -COSPI_16, st[2]);
                    bf[4] <= st[4] + st[5];
                    bf[5] <= st[4] - st[5];
                    bf[6] <= st[7] - st[6];
                    bf[7] <= st[7] + st[6];
                end
            end else if (stage == 3'd3) begin
                // 8-point DCT stage 4: final rotations
                stage <= 4;
                st[0] <= bf[0];
                st[1] <= bf[1];
                st[2] <= bf[2];
                st[3] <= bf[3];
                st[4] <= half_btf( COSPI_56, bf[4],  COSPI_8,  bf[7]);
                st[5] <= half_btf( COSPI_24, bf[5],  COSPI_40, bf[6]);
                st[6] <= half_btf( COSPI_24, bf[6], -COSPI_40, bf[5]);
                st[7] <= half_btf( COSPI_56, bf[7], -COSPI_8,  bf[4]);
            end else if (stage == 3'd4) begin
                // 8-point DCT stage 5: output permutation
                out0 <= st[0];
                out1 <= st[4];
                out2 <= st[2];
                out3 <= st[6];
                out4 <= st[1];
                out5 <= st[5];
                out6 <= st[3];
                out7 <= st[7];
                done  <= 1;
                stage <= 0;
            end
        end
    end

endmodule
