// av1_entropy.v — AV1 Entropy Coder (Bit Packer v1)
// Phase 1: Simple bit-packing for pipeline verification.
//   Collects bits into bytes and outputs them sequentially.
//   encode_bool: packs one bit (bool_val), prob ignored for now
//   encode_lit:  packs lit_bits bits from lit_val (MSB first)
//   finalize:    flushes partial byte with zero padding
//
// Phase 2 (TODO): Upgrade to proper AV1 range coder (od_ec_enc)
//   with CDF-based multi-symbol encoding per SVT-AV1.
//
// Reference: SVT-AV1/Source/Lib/Codec/EbEntropyCoding.c

module av1_entropy (
    input  wire        clk,
    input  wire        rst_n,

    // Control
    input  wire        init,          // Initialize for new frame
    input  wire        encode_bool,   // Encode a single boolean
    input  wire        encode_lit,    // Encode a literal (bypass)
    input  wire        finalize,      // Flush remaining bits

    input  wire        bool_val,      // Boolean value to encode
    input  wire [14:0] bool_prob,     // Probability (unused in v1)
    input  wire [7:0]  lit_val,       // Literal value
    input  wire [4:0]  lit_bits,      // Number of bits for literal (1..24)

    output reg         busy,
    output reg         done,          // Pulsed when operation completes

    // Output byte stream
    output reg         byte_valid,
    output reg  [7:0]  byte_out,
    output reg  [23:0] bytes_written
);

    // Shift register accumulates bits MSB-first
    reg [7:0]  shift_reg;
    reg [3:0]  bit_count;    // 0..7 bits accumulated

    // Literal encoding state
    reg [3:0]  state;
    localparam S_IDLE  = 4'd0;
    localparam S_LIT   = 4'd1;
    localparam S_FINAL = 4'd2;

    reg [4:0]  lit_pos;      // current bit position (counts down)
    reg [7:0]  lit_data;     // latched literal value

    // Push one bit into the shift register, output byte when full
    task push_bit;
        input b;
        begin
            shift_reg <= {shift_reg[6:0], b};
            if (bit_count == 4'd7) begin
                byte_valid <= 1;
                byte_out   <= {shift_reg[6:0], b};
                bytes_written <= bytes_written + 1;
                bit_count  <= 0;
            end else begin
                bit_count <= bit_count + 1;
            end
        end
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            shift_reg     <= 0;
            bit_count     <= 0;
            state         <= S_IDLE;
            busy          <= 0;
            done          <= 0;
            byte_valid    <= 0;
            byte_out      <= 0;
            bytes_written <= 0;
            lit_pos       <= 0;
            lit_data      <= 0;
        end else begin
            done       <= 0;
            byte_valid <= 0;

            case (state)
                S_IDLE: begin
                    if (init) begin
                        shift_reg     <= 0;
                        bit_count     <= 0;
                        bytes_written <= 0;
                        done          <= 1;
                    end else if (encode_bool && !busy) begin
                        // Single bit - encode immediately
                        push_bit(bool_val);
                        done <= 1;
                    end else if (encode_lit && !busy) begin
                        // Multi-bit literal - need loop
                        lit_data <= lit_val;
                        lit_pos  <= lit_bits;
                        busy     <= 1;
                        state    <= S_LIT;
                    end else if (finalize && !busy) begin
                        busy  <= 1;
                        state <= S_FINAL;
                    end
                end

                S_LIT: begin
                    if (lit_pos > 0) begin
                        push_bit(lit_data[lit_pos - 1]);
                        lit_pos <= lit_pos - 1;
                    end else begin
                        busy  <= 0;
                        done  <= 1;
                        state <= S_IDLE;
                    end
                end

                S_FINAL: begin
                    // Flush remaining bits (pad with zeros)
                    if (bit_count > 0) begin
                        byte_valid    <= 1;
                        byte_out      <= shift_reg << (4'd8 - bit_count);
                        bytes_written <= bytes_written + 1;
                        bit_count     <= 0;
                        shift_reg     <= 0;
                    end
                    busy  <= 0;
                    done  <= 1;
                    state <= S_IDLE;
                end
            endcase
        end
    end

endmodule
