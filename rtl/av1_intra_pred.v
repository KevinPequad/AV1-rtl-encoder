// av1_intra_pred.v — AV1 Intra Prediction (DC, V, H, Paeth, Smooth)
// Generates prediction for an 8x8 or 4x4 block.
// Reference: SVT-AV1/Source/Lib/Codec/intra_prediction.c
//
// Modes (from AV1 spec):
//   DC_PRED    = 0  Average of top + left neighbors
//   V_PRED     = 1  Copy top row
//   H_PRED     = 2  Copy left column
//   PAETH_PRED = 3  Paeth predictor (closest of top/left/top-left)
//   SMOOTH_PRED= 4  Weighted average (bi-linear smooth)

module av1_intra_pred (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        is_4x4,
    input  wire [2:0]  mode,          // 0=DC, 1=V, 2=H, 3=Paeth, 4=Smooth
    output reg         done,

    // Neighbor pixels: top row (up to 8) and left column (up to 8)
    input  wire [7:0]  top  [0:7],
    input  wire [7:0]  left [0:7],
    input  wire [7:0]  top_left,      // Corner pixel
    input  wire        has_top,
    input  wire        has_left,

    // Output: predicted block (up to 8x8 = 64 pixels)
    output reg  [7:0]  pred [0:63]
);

    localparam DC_PRED    = 3'd0;
    localparam V_PRED     = 3'd1;
    localparam H_PRED     = 3'd2;
    localparam PAETH_PRED = 3'd3;
    localparam SMOOTH_PRED= 3'd4;

    reg [3:0] blk_size;
    reg [3:0] row, col;
    reg [11:0] dc_sum;
    reg [7:0]  dc_val;
    reg [1:0]  stage;
    reg        active;

    // Paeth helper: absolute value
    function [8:0] abs_diff;
        input [8:0] a;
        input [8:0] b;
        begin
            abs_diff = (a > b) ? (a - b) : (b - a);
        end
    endfunction

    // Smooth prediction weights (from AV1 spec, 8-point)
    // sm_weights[i] = round(256 * (1 - i/N)) for N-point smooth
    wire [7:0] sm_wt_8 [0:7];
    assign sm_wt_8[0] = 8'd255; assign sm_wt_8[1] = 8'd219;
    assign sm_wt_8[2] = 8'd183; assign sm_wt_8[3] = 8'd147;
    assign sm_wt_8[4] = 8'd111; assign sm_wt_8[5] = 8'd75;
    assign sm_wt_8[6] = 8'd39;  assign sm_wt_8[7] = 8'd3;

    wire [7:0] sm_wt_4 [0:3];
    assign sm_wt_4[0] = 8'd255; assign sm_wt_4[1] = 8'd183;
    assign sm_wt_4[2] = 8'd111; assign sm_wt_4[3] = 8'd39;

    integer i;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done   <= 0;
            active <= 0;
            stage  <= 0;
            row    <= 0;
            col    <= 0;
        end else begin
            done <= 0;

            if (start && !active) begin
                active   <= 1;
                blk_size <= is_4x4 ? 4'd4 : 4'd8;
                stage    <= 0;
                row      <= 0;
                col      <= 0;

                // Compute DC value
                dc_sum <= 0;
                if (mode == DC_PRED) begin
                    dc_sum <= 0;
                    stage  <= 0; // will compute DC in stage 0
                end
            end else if (active) begin
                case (stage)
                    2'd0: begin
                        // Compute DC sum using blocking variable
                        begin
                            reg [11:0] sum;
                            sum = 12'd0;
                            for (i = 0; i < 8; i = i + 1) begin
                                if (i < blk_size) begin
                                    if (has_top)  sum = sum + {4'd0, top[i]};
                                    if (has_left) sum = sum + {4'd0, left[i]};
                                end
                            end
                            dc_sum <= sum;
                        end
                        stage <= 1;
                    end
                    2'd1: begin
                        // Finish DC computation
                        if (has_top && has_left)
                            dc_val <= dc_sum / (blk_size * 2);
                        else if (has_top || has_left)
                            dc_val <= dc_sum / blk_size;
                        else
                            dc_val <= 8'd128;
                        stage <= 2;
                    end
                    2'd2: begin
                        // Generate prediction pixels row by row
                        for (i = 0; i < 8; i = i + 1) begin
                            if (i < blk_size) begin
                                case (mode)
                                    DC_PRED: begin
                                        pred[row * blk_size + i] <= dc_val;
                                    end
                                    V_PRED: begin
                                        pred[row * blk_size + i] <= has_top ? top[i] : 8'd128;
                                    end
                                    H_PRED: begin
                                        pred[row * blk_size + i] <= has_left ? left[row] : 8'd128;
                                    end
                                    PAETH_PRED: begin
                                        // Paeth: predict closest of top, left, top_left
                                        // base = top[i] + left[row] - top_left
                                        // pick whichever of top[i], left[row], top_left
                                        // is closest to base
                                        pred[row * blk_size + i] <=
                                            (abs_diff({1'b0,top[i]} + {1'b0,left[row]} - {1'b0,top_left}, {1'b0,left[row]}) <=
                                             abs_diff({1'b0,top[i]} + {1'b0,left[row]} - {1'b0,top_left}, {1'b0,top[i]}))
                                            ? left[row] : top[i];
                                    end
                                    SMOOTH_PRED: begin
                                        // Smooth: weighted average
                                        if (is_4x4)
                                            pred[row * blk_size + i] <=
                                                (sm_wt_4[row] * top[i] + (8'd255 - sm_wt_4[row]) * left[row] + 128) >> 8;
                                        else
                                            pred[row * blk_size + i] <=
                                                (sm_wt_8[row] * top[i] + (8'd255 - sm_wt_8[row]) * left[row] + 128) >> 8;
                                    end
                                    default: begin
                                        pred[row * blk_size + i] <= 8'd128;
                                    end
                                endcase
                            end
                        end

                        if (row == blk_size - 1) begin
                            active <= 0;
                            done   <= 1;
                            stage  <= 0;
                        end else begin
                            row <= row + 1;
                        end
                    end
                endcase
            end
        end
    end

endmodule
