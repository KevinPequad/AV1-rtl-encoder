// av1_bitstream.v — AV1 OBU Bitstream Generator
// Generates OBU (Open Bitstream Unit) headers and fixed bitstream structures:
//   - Temporal Delimiter OBU
//   - Sequence Header OBU
//   - Frame Header (within Frame OBU)
//
// Reference: AV1 Specification Section 5 (OBU syntax)
//            SVT-AV1/Source/Lib/Codec/bitstream_unit.c

module av1_bitstream #(
    parameter FRAME_WIDTH  = 1280,
    parameter FRAME_HEIGHT = 720
) (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        write_td,         // Write temporal delimiter OBU
    input  wire        write_seq_hdr,    // Write sequence header OBU
    input  wire        write_frame_hdr,  // Write frame header
    input  wire        is_keyframe,
    input  wire [7:0]  qindex,
    input  wire [3:0]  frame_num,

    output reg         busy,
    output reg         done,

    // Output byte stream
    output reg         byte_valid,
    output reg  [7:0]  byte_out,
    output reg  [23:0] bytes_written
);

    reg [7:0]  obuf [0:63];   // Output buffer
    reg [5:0]  obuf_len;
    reg [5:0]  obuf_idx;
    reg [2:0]  state;

    localparam S_IDLE  = 3'd0;
    localparam S_BUILD = 3'd1;
    localparam S_OUT   = 3'd2;

    // Latch which build operation was requested
    reg [1:0]  build_cmd;
    localparam CMD_SEQ = 2'd1;
    localparam CMD_FRM = 2'd2;

    // Latch frame header inputs
    reg        lat_is_keyframe;
    reg [7:0]  lat_qindex;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            busy          <= 0;
            done          <= 0;
            byte_valid    <= 0;
            bytes_written <= 0;
            state         <= S_IDLE;
            obuf_len      <= 0;
            obuf_idx      <= 0;
            build_cmd     <= 0;
        end else begin
            done       <= 0;
            byte_valid <= 0;

            case (state)
                S_IDLE: begin
                    if (write_td) begin
                        // Temporal Delimiter OBU: type=2, has_size=1
                        // Header: 0b0_0010_0_1_0 = 0x12, size = 0
                        obuf[0]  <= 8'h12;
                        obuf[1]  <= 8'h00;
                        obuf_len <= 6'd2;
                        obuf_idx <= 0;
                        busy     <= 1;
                        state    <= S_OUT;
                    end else if (write_seq_hdr) begin
                        build_cmd <= CMD_SEQ;
                        busy      <= 1;
                        state     <= S_BUILD;
                    end else if (write_frame_hdr) begin
                        build_cmd       <= CMD_FRM;
                        lat_is_keyframe <= is_keyframe;
                        lat_qindex      <= qindex;
                        busy            <= 1;
                        state           <= S_BUILD;
                    end
                end

                S_BUILD: begin
                    obuf_idx <= 0;

                    if (build_cmd == CMD_SEQ) begin
                        // Sequence Header OBU (simplified placeholder)
                        // OBU header: type=1(seq_hdr), has_size=1
                        // 0b0_0001_0_1_0 = 0x0A
                        obuf[0]  <= 8'h0A;
                        obuf[1]  <= 8'd16;  // size in leb128 (placeholder)
                        obuf[2]  <= 8'h00;
                        obuf[3]  <= 8'h00;
                        obuf[4]  <= 8'h00;
                        obuf[5]  <= 8'h00;
                        obuf[6]  <= 8'h40;
                        obuf[7]  <= 8'h00;
                        obuf[8]  <= FRAME_WIDTH[15:8];
                        obuf[9]  <= FRAME_WIDTH[7:0];
                        obuf[10] <= FRAME_HEIGHT[15:8];
                        obuf[11] <= FRAME_HEIGHT[7:0];
                        obuf[12] <= 8'h00;
                        obuf[13] <= 8'h00;
                        obuf[14] <= 8'h00;
                        obuf[15] <= 8'h00;
                        obuf[16] <= 8'h00;
                        obuf[17] <= 8'h00;
                        obuf_len <= 6'd18;
                    end else begin
                        // Frame OBU header: type=6(FRAME), has_size=1
                        // 0b0_0110_0_1_0 = 0x32
                        obuf[0]  <= 8'h32;
                        obuf[1]  <= 8'h04;  // size placeholder
                        obuf[2]  <= lat_is_keyframe ? 8'h10 : 8'h20;
                        obuf[3]  <= lat_qindex;
                        obuf[4]  <= 8'h00;
                        obuf[5]  <= 8'h00;
                        obuf_len <= 6'd6;
                    end

                    state <= S_OUT;
                end

                S_OUT: begin
                    if (obuf_idx < obuf_len) begin
                        byte_valid    <= 1;
                        byte_out      <= obuf[obuf_idx];
                        obuf_idx      <= obuf_idx + 1;
                        bytes_written <= bytes_written + 1;
                    end else begin
                        busy  <= 0;
                        done  <= 1;
                        state <= S_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
