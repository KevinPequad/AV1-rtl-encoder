// av1_inverse_transform.v — AV1 Inverse DCT-II Transform (4x4 and 8x8)
// Mirrors av1_transform.v but uses the inverse butterfly structure.
// Reference: SVT-AV1/Source/Lib/Codec/inv_transforms.c
//   svt_av1_idct4_new() and svt_av1_idct8_new()

module av1_inverse_transform (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        is_4x4,
    output reg         done,

    input  wire signed [15:0] in0, in1, in2, in3,
    input  wire signed [15:0] in4, in5, in6, in7,

    output reg  signed [15:0] out0, out1, out2, out3,
    output reg  signed [15:0] out4, out5, out6, out7
);

    localparam COS_BIT = 12;  // inverse uses cos_bit=12

    // cospi at cos_bit=12 from SVT-AV1 cospi_arr_data[2]
    localparam signed [15:0] COSPI_8  = 16'sd4017;
    localparam signed [15:0] COSPI_16 = 16'sd3784;
    localparam signed [15:0] COSPI_24 = 16'sd3406;
    localparam signed [15:0] COSPI_32 = 16'sd2896;
    localparam signed [15:0] COSPI_40 = 16'sd2276;
    localparam signed [15:0] COSPI_48 = 16'sd1567;
    localparam signed [15:0] COSPI_56 = 16'sd799;

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

    reg [2:0] stage;
    reg signed [15:0] bf [0:7];
    reg signed [15:0] st [0:7];

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
                    // 4-pt IDCT stage 1: input permutation
                    bf[0] <= in0;  // DC
                    bf[1] <= in2;
                    bf[2] <= in1;
                    bf[3] <= in3;
                end else begin
                    // 8-pt IDCT stage 1: input permutation
                    bf[0] <= in0;
                    bf[1] <= in4;
                    bf[2] <= in2;
                    bf[3] <= in6;
                    bf[4] <= in1;
                    bf[5] <= in5;
                    bf[6] <= in3;
                    bf[7] <= in7;
                end
            end else if (stage == 3'd1) begin
                stage <= 2;
                if (is_4x4) begin
                    // 4-pt IDCT stage 2: rotations
                    st[0] <= half_btf( COSPI_32, bf[0], COSPI_32, bf[1]);
                    st[1] <= half_btf( COSPI_32, bf[0],-COSPI_32, bf[1]);
                    st[2] <= half_btf( COSPI_48, bf[2],-COSPI_16, bf[3]);
                    st[3] <= half_btf( COSPI_16, bf[2], COSPI_48, bf[3]);
                end else begin
                    // 8-pt IDCT stage 2: rotations on high part
                    st[0] <= bf[0];
                    st[1] <= bf[1];
                    st[2] <= bf[2];
                    st[3] <= bf[3];
                    st[4] <= half_btf( COSPI_56, bf[4],-COSPI_8,  bf[7]);
                    st[5] <= half_btf( COSPI_24, bf[5],-COSPI_40, bf[6]);
                    st[6] <= half_btf( COSPI_40, bf[5], COSPI_24, bf[6]);
                    st[7] <= half_btf( COSPI_8,  bf[4], COSPI_56, bf[7]);
                end
            end else if (stage == 3'd2) begin
                stage <= 3;
                if (is_4x4) begin
                    // 4-pt IDCT stage 3: output butterflies
                    out0 <= st[0] + st[3];
                    out1 <= st[1] + st[2];
                    out2 <= st[1] - st[2];
                    out3 <= st[0] - st[3];
                    done  <= 1;
                    stage <= 0;
                end else begin
                    // 8-pt IDCT stage 3: low part rotations + high part butterflies
                    bf[0] <= half_btf( COSPI_32, st[0], COSPI_32, st[1]);
                    bf[1] <= half_btf( COSPI_32, st[0],-COSPI_32, st[1]);
                    bf[2] <= half_btf( COSPI_48, st[2],-COSPI_16, st[3]);
                    bf[3] <= half_btf( COSPI_16, st[2], COSPI_48, st[3]);
                    bf[4] <= st[4] + st[5];
                    bf[5] <= st[4] - st[5];
                    bf[6] <= st[7] - st[6];
                    bf[7] <= st[7] + st[6];
                end
            end else if (stage == 3'd3) begin
                // 8-pt IDCT stage 4: more butterflies
                stage <= 4;
                st[0] <= bf[0] + bf[3];
                st[1] <= bf[1] + bf[2];
                st[2] <= bf[1] - bf[2];
                st[3] <= bf[0] - bf[3];
                st[4] <= bf[4];
                st[5] <= half_btf(-COSPI_32, bf[5], COSPI_32, bf[6]);
                st[6] <= half_btf( COSPI_32, bf[6], COSPI_32, bf[5]);
                st[7] <= bf[7];
            end else if (stage == 3'd4) begin
                // 8-pt IDCT stage 5: final output
                out0 <= st[0] + st[7];
                out1 <= st[1] + st[6];
                out2 <= st[2] + st[5];
                out3 <= st[3] + st[4];
                out4 <= st[3] - st[4];
                out5 <= st[2] - st[5];
                out6 <= st[1] - st[6];
                out7 <= st[0] - st[7];
                done  <= 1;
                stage <= 0;
            end
        end
    end

endmodule
