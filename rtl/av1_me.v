// av1_me.v — AV1 Motion Estimation (Full-pel SAD search)
// Searches a reference frame for the best matching 8x8 block.
// Uses a diamond/full search pattern within ±search_range.
// Reference: SVT-AV1/Source/Lib/Codec/av1me.c

module av1_me #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720,
    parameter SEARCH_RANGE = 16    // ±16 pixels
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    output reg         done,

    // Current block position (pixel coordinates)
    input  wire [10:0] cur_x,
    input  wire [10:0] cur_y,

    // Current block pixels (8x8 = 64 bytes)
    input  wire [7:0]  cur_blk [0:63],

    // Reference frame memory interface
    output reg  [19:0] ref_mem_addr,
    input  wire [7:0]  ref_mem_data,

    // Best match results
    output reg signed [8:0] best_mvx,
    output reg signed [8:0] best_mvy,
    output reg        [17:0] best_sad
);

    localparam LUMA_SIZE = FRAME_WIDTH * FRAME_HEIGHT;

    reg [4:0]  state;
    localparam S_IDLE      = 5'd0;
    localparam S_INIT      = 5'd1;
    localparam S_FETCH_REF = 5'd2;
    localparam S_WAIT_MEM  = 5'd3;
    localparam S_COMPUTE   = 5'd4;
    localparam S_NEXT_PIX  = 5'd5;
    localparam S_NEXT_MV   = 5'd6;
    localparam S_DONE      = 5'd7;

    reg signed [8:0] mv_x, mv_y;
    reg [5:0]  pix_idx;       // 0..63 within 8x8 block
    reg [17:0] cur_sad;
    reg [7:0]  ref_pixel;
    reg [10:0] ref_x, ref_y;

    wire signed [10:0] cand_x = $signed({1'b0, cur_x}) + mv_x;
    wire signed [10:0] cand_y = $signed({1'b0, cur_y}) + mv_y;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done     <= 0;
            state    <= S_IDLE;
            best_mvx <= 0;
            best_mvy <= 0;
            best_sad <= 18'h3FFFF;
        end else begin
            done <= 0;

            case (state)
                S_IDLE: begin
                    if (start) begin
                        state    <= S_INIT;
                        mv_x     <= -SEARCH_RANGE;
                        mv_y     <= -SEARCH_RANGE;
                        best_sad <= 18'h3FFFF;
                        best_mvx <= 0;
                        best_mvy <= 0;
                    end
                end

                S_INIT: begin
                    // Check bounds
                    if (cand_x < 0 || cand_y < 0 ||
                        cand_x + 8 > FRAME_WIDTH || cand_y + 8 > FRAME_HEIGHT) begin
                        state <= S_NEXT_MV;
                    end else begin
                        pix_idx <= 0;
                        cur_sad <= 0;
                        state   <= S_FETCH_REF;
                    end
                end

                S_FETCH_REF: begin
                    // Compute address for current pixel in reference
                    ref_x <= cand_x + (pix_idx & 6'd7);  // pix_idx % 8
                    ref_y <= cand_y + (pix_idx >> 3);     // pix_idx / 8
                    ref_mem_addr <= (cand_y + (pix_idx >> 3)) * FRAME_WIDTH +
                                    (cand_x + (pix_idx & 6'd7));
                    state <= S_WAIT_MEM;
                end

                S_WAIT_MEM: begin
                    // One cycle memory latency
                    state <= S_COMPUTE;
                end

                S_COMPUTE: begin
                    // Accumulate SAD
                    ref_pixel <= ref_mem_data;
                    if (cur_blk[pix_idx] > ref_mem_data)
                        cur_sad <= cur_sad + (cur_blk[pix_idx] - ref_mem_data);
                    else
                        cur_sad <= cur_sad + (ref_mem_data - cur_blk[pix_idx]);
                    state <= S_NEXT_PIX;
                end

                S_NEXT_PIX: begin
                    // Early termination if SAD already exceeds best
                    if (cur_sad >= best_sad) begin
                        state <= S_NEXT_MV;
                    end else if (pix_idx == 6'd63) begin
                        // Finished all 64 pixels
                        if (cur_sad < best_sad) begin
                            best_sad <= cur_sad;
                            best_mvx <= mv_x;
                            best_mvy <= mv_y;
                        end
                        state <= S_NEXT_MV;
                    end else begin
                        pix_idx <= pix_idx + 1;
                        state   <= S_FETCH_REF;
                    end
                end

                S_NEXT_MV: begin
                    if (mv_x == SEARCH_RANGE && mv_y == SEARCH_RANGE) begin
                        state <= S_DONE;
                    end else begin
                        if (mv_x < SEARCH_RANGE)
                            mv_x <= mv_x + 1;
                        else begin
                            mv_x <= -SEARCH_RANGE;
                            mv_y <= mv_y + 1;
                        end
                        state <= S_INIT;
                    end
                end

                S_DONE: begin
                    done  <= 1;
                    state <= S_IDLE;
                end
            endcase
        end
    end

endmodule
