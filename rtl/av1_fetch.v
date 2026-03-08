// av1_fetch.v — Fetch block pixels from raw frame memory
// Reads an NxN block (4x4 or 8x8) from YUV420 planar memory.
// For luma: reads 8x8 block at (mb_x*8, mb_y*8) within the frame.
// For chroma: reads 4x4 block at (mb_x*4, mb_y*4) within the chroma plane.

module av1_fetch #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        is_chroma,    // 0=luma, 1=chroma
    input  wire        chroma_id,    // 0=Cb, 1=Cr
    output reg         done,

    // Block position (in 8x8 block units for luma, 4x4 for chroma)
    input  wire [9:0]  blk_x,
    input  wire [9:0]  blk_y,

    // Raw frame memory interface
    output reg  [20:0] mem_addr,
    input  wire [7:0]  mem_data,

    // Output: 8x8 block buffer (64 bytes) or 4x4 (16 bytes)
    output reg  [7:0]  pixel_buf [0:63],
    output reg  [5:0]  pixel_count
);

    localparam LUMA_SIZE   = FRAME_WIDTH * FRAME_HEIGHT;
    localparam CHROMA_W    = FRAME_WIDTH / 2;
    localparam CHROMA_H    = FRAME_HEIGHT / 2;
    localparam CHROMA_SIZE = CHROMA_W * CHROMA_H;
    localparam CB_BASE     = LUMA_SIZE;
    localparam CR_BASE     = LUMA_SIZE + CHROMA_SIZE;

    reg [5:0] row, col;
    reg [5:0] blk_size;  // 8 for luma, 4 for chroma
    reg [20:0] row_base;
    reg        active;
    reg [1:0]  wait_cnt;  // wait for memory latency

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done        <= 0;
            active      <= 0;
            pixel_count <= 0;
            mem_addr    <= 0;
            row <= 0; col <= 0;
        end else begin
            done <= 0;

            if (start && !active) begin
                active   <= 1;
                row      <= 0;
                col      <= 0;
                wait_cnt <= 0;
                pixel_count <= 0;
                blk_size <= is_chroma ? 6'd4 : 6'd8;

                // Compute base address for first pixel
                if (!is_chroma) begin
                    // Luma: Y plane starts at 0
                    row_base <= blk_y * 8 * FRAME_WIDTH + blk_x * 8;
                    mem_addr <= blk_y * 8 * FRAME_WIDTH + blk_x * 8;
                end else if (!chroma_id) begin
                    // Cb
                    row_base <= CB_BASE + blk_y * 4 * CHROMA_W + blk_x * 4;
                    mem_addr <= CB_BASE + blk_y * 4 * CHROMA_W + blk_x * 4;
                end else begin
                    // Cr
                    row_base <= CR_BASE + blk_y * 4 * CHROMA_W + blk_x * 4;
                    mem_addr <= CR_BASE + blk_y * 4 * CHROMA_W + blk_x * 4;
                end
            end else if (active) begin
                if (wait_cnt < 2'd1) begin
                    // Wait one cycle for memory read latency
                    wait_cnt <= wait_cnt + 1;
                end else begin
                    // Store pixel
                    pixel_buf[row * blk_size + col] <= mem_data;
                    pixel_count <= pixel_count + 1;
                    wait_cnt <= 0;

                    if (col == blk_size - 1) begin
                        col <= 0;
                        if (row == blk_size - 1) begin
                            // Done fetching all pixels
                            active <= 0;
                            done   <= 1;
                        end else begin
                            row <= row + 1;
                            // Advance to next row
                            if (!is_chroma)
                                row_base <= row_base + FRAME_WIDTH;
                            else
                                row_base <= row_base + CHROMA_W;
                            mem_addr <= row_base + (is_chroma ? CHROMA_W : FRAME_WIDTH);
                        end
                    end else begin
                        col <= col + 1;
                        mem_addr <= mem_addr + 1;
                    end
                end
            end
        end
    end

endmodule
