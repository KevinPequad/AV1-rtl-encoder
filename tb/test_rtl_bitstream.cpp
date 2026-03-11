// test_rtl_bitstream.cpp -- Standalone regression for rtl/av1_bitstream.v

#include <verilated.h>
#include "Vav1_bitstream.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef FRAME_W
#define FRAME_W 1280
#endif
#ifndef FRAME_H
#define FRAME_H 720
#endif

static constexpr int FRAME_WIDTH = FRAME_W;
static constexpr int FRAME_HEIGHT = FRAME_H;

namespace {

struct BitWriter {
    std::vector<uint8_t> bytes;
    int bit_pos = 0;

    void write_bit(int bit) {
        if (bit_pos == 0) bytes.push_back(0);
        if (bit & 1) bytes.back() |= static_cast<uint8_t>(1u << (7 - bit_pos));
        bit_pos = (bit_pos + 1) & 7;
    }

    void write_bits(int value, int nbits) {
        for (int i = nbits - 1; i >= 0; --i) write_bit((value >> i) & 1);
    }

    void write_trailing_bits() {
        write_bit(1);
        while (bit_pos != 0) write_bit(0);
    }

    void flush_zero_pad() {
        if (bit_pos != 0) bit_pos = 0;
    }
};

static int bits_needed(int val) {
    int bits = 0;
    int tmp = val - 1;
    while (tmp > 0) {
        ++bits;
        tmp >>= 1;
    }
    return bits < 1 ? 1 : bits;
}

static int tile_log2(int blk_size, int target) {
    int k = 0;
    while ((blk_size << k) < target) ++k;
    return k;
}

static void write_color_config(BitWriter& bw) {
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bits(0, 2);
    bw.write_bit(0);
}

static void write_tile_info(BitWriter& bw) {
    const int mi_cols_aligned = ((FRAME_WIDTH / 4) + 15) & ~15;
    const int mi_rows_aligned = ((FRAME_HEIGHT / 4) + 15) & ~15;
    const int sb_cols = mi_cols_aligned >> 4;
    const int sb_rows = mi_rows_aligned >> 4;
    const int min_log2_tile_cols = tile_log2(64, sb_cols);
    const int sb_cols_min = std::min(sb_cols, 64);
    const int sb_rows_min = std::min(sb_rows, 64);
    const int max_log2_tile_cols = tile_log2(1, sb_cols_min);
    const int max_log2_tile_rows = tile_log2(1, sb_rows_min);
    int min_log2_tiles = tile_log2(576, sb_cols * sb_rows);
    if (min_log2_tile_cols > min_log2_tiles) min_log2_tiles = min_log2_tile_cols;
    bw.write_bit(1);
    const int tile_cols_log2 = min_log2_tile_cols;
    if (tile_cols_log2 < max_log2_tile_cols) bw.write_bit(0);
    int min_log2_tile_rows = min_log2_tiles - tile_cols_log2;
    if (min_log2_tile_rows < 0) min_log2_tile_rows = 0;
    const int tile_rows_log2 = min_log2_tile_rows;
    if (tile_rows_log2 < max_log2_tile_rows) bw.write_bit(0);
}

static void write_quantization_params(BitWriter& bw, uint8_t qindex) {
    bw.write_bits(qindex, 8);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
}

static void write_loop_filter_params(BitWriter& bw) {
    bw.write_bits(0, 6);
    bw.write_bits(0, 6);
    bw.write_bits(0, 3);
    bw.write_bit(0);
}

static std::vector<uint8_t> wrap_obu(uint8_t header, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.push_back(header);
    out.push_back(static_cast<uint8_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static std::vector<uint8_t> wrap_obu_fixed_leb128(uint8_t header,
                                                  const std::vector<uint8_t>& payload,
                                                  int width_bytes) {
    std::vector<uint8_t> out;
    out.push_back(header);
    size_t val = payload.size();
    for (int i = 0; i < width_bytes; ++i) {
        uint8_t byte = static_cast<uint8_t>(val & 0x7F);
        val >>= 7;
        if (i != width_bytes - 1) byte |= 0x80;
        out.push_back(byte);
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static std::vector<uint8_t> build_expected_seq() {
    BitWriter bw;
    bw.write_bits(0, 3);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bits(0, 5);
    bw.write_bits(0, 12);
    bw.write_bits(4, 5);
    const int w_bits = bits_needed(FRAME_WIDTH);
    const int h_bits = bits_needed(FRAME_HEIGHT);
    bw.write_bits(w_bits - 1, 4);
    bw.write_bits(h_bits - 1, 4);
    bw.write_bits(FRAME_WIDTH - 1, w_bits);
    bw.write_bits(FRAME_HEIGHT - 1, h_bits);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    write_color_config(bw);
    bw.write_bit(0);
    bw.write_trailing_bits();
    return wrap_obu(0x0A, bw.bytes);
}

static std::vector<uint8_t> build_expected_key(uint8_t qindex) {
    BitWriter bw;
    bw.write_bit(0);
    bw.write_bits(0, 2);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    write_tile_info(bw);
    write_quantization_params(bw, qindex);
    bw.write_bit(0);
    bw.write_bit(0);
    write_loop_filter_params(bw);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.flush_zero_pad();
    return wrap_obu_fixed_leb128(0x32, bw.bytes, 4);
}

static std::vector<uint8_t> build_expected_inter(uint8_t qindex) {
    BitWriter bw;
    bw.write_bit(0);
    bw.write_bits(1, 2);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bits(0x01, 8);
    for (int ref = 0; ref < 7; ++ref) bw.write_bits(0, 3);
    bw.write_bit(0);
    bw.write_bit(1);
    bw.write_bit(0);
    bw.write_bits(0, 2);
    bw.write_bit(0);
    write_tile_info(bw);
    write_quantization_params(bw, qindex);
    bw.write_bit(0);
    bw.write_bit(0);
    write_loop_filter_params(bw);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    for (int ref = 0; ref < 7; ++ref) bw.write_bit(0);
    bw.flush_zero_pad();
    return wrap_obu_fixed_leb128(0x32, bw.bytes, 4);
}

static std::string hex_string(const std::vector<uint8_t>& bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0xF]);
    }
    return out;
}

static void tick(Vav1_bitstream* dut, std::vector<uint8_t>& out) {
    dut->clk = 1;
    dut->eval();
    if (dut->byte_valid) out.push_back(static_cast<uint8_t>(dut->byte_out));
    dut->clk = 0;
    dut->eval();
}

static std::vector<uint8_t> run_command(bool seq_hdr, bool frame_hdr, bool is_keyframe, uint8_t qindex) {
    Vav1_bitstream dut;
    std::vector<uint8_t> out;
    dut.clk = 0;
    dut.rst_n = 0;
    dut.write_td = 0;
    dut.write_seq_hdr = 0;
    dut.write_frame_hdr = 0;
    dut.is_keyframe = is_keyframe ? 1 : 0;
    dut.qindex = qindex;
    dut.frame_num = 0;
    dut.eval();

    for (int i = 0; i < 4; ++i) tick(&dut, out);
    dut.rst_n = 1;
    tick(&dut, out);

    dut.write_seq_hdr = seq_hdr ? 1 : 0;
    dut.write_frame_hdr = frame_hdr ? 1 : 0;
    dut.is_keyframe = is_keyframe ? 1 : 0;
    dut.qindex = qindex;
    tick(&dut, out);
    dut.write_seq_hdr = 0;
    dut.write_frame_hdr = 0;

    for (int guard = 0; guard < 256; ++guard) {
        tick(&dut, out);
        if (dut.done) break;
    }
    return out;
}

static bool expect_eq(const char* label, const std::vector<uint8_t>& actual,
                      const std::vector<uint8_t>& expected) {
    if (actual == expected) {
        std::fprintf(stderr, "[PASS] %s bytes=%zu\n", label, actual.size());
        return true;
    }
    std::fprintf(stderr, "[FAIL] %s\n", label);
    std::fprintf(stderr, "  actual  : %s\n", hex_string(actual).c_str());
    std::fprintf(stderr, "  expected: %s\n", hex_string(expected).c_str());
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const uint8_t qindex = 128;

    bool ok = true;
    ok &= expect_eq("sequence_header",
                    run_command(true, false, true, qindex),
                    build_expected_seq());
    ok &= expect_eq("video_keyframe_header",
                    run_command(false, true, true, qindex),
                    build_expected_key(qindex));
    ok &= expect_eq("video_inter_header",
                    run_command(false, true, false, qindex),
                    build_expected_inter(qindex));

    return ok ? 0 : 1;
}
