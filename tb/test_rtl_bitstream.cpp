// test_rtl_bitstream.cpp -- Standalone regression for rtl/av1_bitstream.v

#include <verilated.h>
#include "Vav1_bitstream.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
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

// P9 disabled-filter policy shared by the RTL header generator and top-level
// reference-ownership testbench. The standalone bitstream regression parses
// these fields explicitly so enabling any post-recon filter or switchable
// motion mode trips this gate before unfiltered/simple-motion RTL reconstruction
// can be mistaken for filtered/OBMC public-decoder output.
static constexpr int P9_ENABLE_CDEF = 0;
static constexpr int P9_ENABLE_RESTORATION = 0;
static constexpr int P9_LOOP_FILTER_LEVEL_0 = 0;
static constexpr int P9_LOOP_FILTER_LEVEL_1 = 0;
static constexpr int P9_LOOP_FILTER_SHARPNESS = 0;
static constexpr int P9_LOOP_FILTER_DELTA_ENABLED = 0;

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

struct BitReader {
    const std::vector<uint8_t>& bytes;
    size_t byte_idx = 0;
    int bit_pos = 0;

    explicit BitReader(const std::vector<uint8_t>& data) : bytes(data) {}

    bool read_bit(int& out) {
        if (byte_idx >= bytes.size()) return false;
        out = (bytes[byte_idx] >> (7 - bit_pos)) & 1;
        bit_pos = (bit_pos + 1) & 7;
        if (bit_pos == 0) ++byte_idx;
        return true;
    }

    bool read_bits(int nbits, uint32_t& out) {
        out = 0;
        for (int i = 0; i < nbits; ++i) {
            int bit = 0;
            if (!read_bit(bit)) return false;
            out = (out << 1) | static_cast<uint32_t>(bit);
        }
        return true;
    }
};

static bool fail_policy(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

static bool expect_bit(BitReader& br, int expected, const char* name, std::string* err) {
    int got = 0;
    if (!br.read_bit(got)) {
        return fail_policy(err, std::string("short read at ") + name);
    }
    if (got != expected) {
        std::ostringstream os;
        os << name << " expected " << expected << " got " << got;
        return fail_policy(err, os.str());
    }
    return true;
}

static bool expect_bits(BitReader& br, uint32_t expected, int nbits,
                        const char* name, std::string* err) {
    uint32_t got = 0;
    if (!br.read_bits(nbits, got)) {
        return fail_policy(err, std::string("short read at ") + name);
    }
    if (got != expected) {
        std::ostringstream os;
        os << name << " expected " << expected << " got " << got;
        return fail_policy(err, os.str());
    }
    return true;
}

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

static bool read_tile_info(BitReader& br, std::string* err) {
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
    if (!expect_bit(br, 1, "uniform_tile_spacing_flag", err)) return false;
    const int tile_cols_log2 = min_log2_tile_cols;
    if (tile_cols_log2 < max_log2_tile_cols &&
        !expect_bit(br, 0, "increment_tile_cols_log2", err)) return false;
    int min_log2_tile_rows = min_log2_tiles - tile_cols_log2;
    if (min_log2_tile_rows < 0) min_log2_tile_rows = 0;
    const int tile_rows_log2 = min_log2_tile_rows;
    if (tile_rows_log2 < max_log2_tile_rows &&
        !expect_bit(br, 0, "increment_tile_rows_log2", err)) return false;
    return true;
}

static void write_quantization_params(BitWriter& bw, uint8_t qindex) {
    bw.write_bits(qindex, 8);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.write_bit(0);
}

static bool read_quantization_params(BitReader& br, uint8_t qindex, std::string* err) {
    return expect_bits(br, qindex, 8, "base_q_idx", err) &&
           expect_bit(br, 0, "delta_q_y_dc", err) &&
           expect_bit(br, 0, "diff_uv_delta", err) &&
           expect_bit(br, 0, "delta_q_u_dc", err) &&
           expect_bit(br, 0, "delta_q_u_ac", err) &&
           expect_bit(br, 0, "using_qmatrix", err);
}

static void write_loop_filter_params(BitWriter& bw,
                                     int level0 = P9_LOOP_FILTER_LEVEL_0,
                                     int level1 = P9_LOOP_FILTER_LEVEL_1,
                                     int sharpness = P9_LOOP_FILTER_SHARPNESS,
                                     int delta_enabled = P9_LOOP_FILTER_DELTA_ENABLED) {
    bw.write_bits(level0, 6);
    bw.write_bits(level1, 6);
    bw.write_bits(sharpness, 3);
    bw.write_bit(delta_enabled);
}

static bool read_disabled_loop_filter_params(BitReader& br, std::string* err) {
    uint32_t level0 = 0;
    uint32_t level1 = 0;
    uint32_t sharpness = 0;
    int delta_enabled = 0;
    if (!br.read_bits(6, level0)) return fail_policy(err, "short read at loop_filter_level[0]");
    if (!br.read_bits(6, level1)) return fail_policy(err, "short read at loop_filter_level[1]");
    if (level0 != P9_LOOP_FILTER_LEVEL_0 || level1 != P9_LOOP_FILTER_LEVEL_1) {
        std::ostringstream os;
        os << "loop_filter_level[0..1] expected 0,0 got " << level0 << "," << level1;
        return fail_policy(err, os.str());
    }
    if (!br.read_bits(3, sharpness)) return fail_policy(err, "short read at loop_filter_sharpness");
    if (!br.read_bit(delta_enabled)) return fail_policy(err, "short read at loop_filter_delta_enabled");
    if (sharpness != P9_LOOP_FILTER_SHARPNESS ||
        delta_enabled != P9_LOOP_FILTER_DELTA_ENABLED) {
        std::ostringstream os;
        os << "loop filter sharpness/delta expected 0,0 got "
           << sharpness << "," << delta_enabled;
        return fail_policy(err, os.str());
    }
    return true;
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

static bool extract_payload_one_byte(const std::vector<uint8_t>& obu, uint8_t expected_header,
                                     std::vector<uint8_t>& payload, std::string* err) {
    if (obu.size() < 2) return fail_policy(err, "OBU too short");
    if (obu[0] != expected_header) {
        std::ostringstream os;
        os << "OBU header expected 0x" << std::hex << static_cast<int>(expected_header)
           << " got 0x" << static_cast<int>(obu[0]);
        return fail_policy(err, os.str());
    }
    const size_t len = obu[1];
    if (obu.size() != len + 2) {
        std::ostringstream os;
        os << "OBU one-byte payload length expected " << len
           << " bytes, total got " << obu.size();
        return fail_policy(err, os.str());
    }
    payload.assign(obu.begin() + 2, obu.end());
    return true;
}

static bool extract_payload_fixed_leb128(const std::vector<uint8_t>& obu, uint8_t expected_header,
                                         int width_bytes,
                                         std::vector<uint8_t>& payload, std::string* err) {
    if (obu.size() < static_cast<size_t>(1 + width_bytes)) return fail_policy(err, "OBU too short");
    if (obu[0] != expected_header) {
        std::ostringstream os;
        os << "OBU header expected 0x" << std::hex << static_cast<int>(expected_header)
           << " got 0x" << static_cast<int>(obu[0]);
        return fail_policy(err, os.str());
    }
    size_t len = 0;
    for (int i = 0; i < width_bytes; ++i) {
        len |= static_cast<size_t>(obu[1 + i] & 0x7F) << (7 * i);
        const bool should_continue = i != width_bytes - 1;
        if (((obu[1 + i] & 0x80) != 0) != should_continue) {
            return fail_policy(err, "frame OBU size is not fixed-width LEB128");
        }
    }
    if (obu.size() != len + 1 + static_cast<size_t>(width_bytes)) {
        std::ostringstream os;
        os << "OBU fixed LEB128 payload length expected " << len
           << " bytes, total got " << obu.size();
        return fail_policy(err, os.str());
    }
    payload.assign(obu.begin() + 1 + width_bytes, obu.end());
    return true;
}

static std::vector<uint8_t> build_expected_seq(int enable_cdef = P9_ENABLE_CDEF,
                                               int enable_restoration = P9_ENABLE_RESTORATION) {
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
    bw.write_bit(enable_cdef);
    bw.write_bit(enable_restoration);
    write_color_config(bw);
    bw.write_bit(0);
    bw.write_trailing_bits();
    return wrap_obu(0x0A, bw.bytes);
}

static std::vector<uint8_t> build_expected_key(uint8_t qindex,
                                               int loop_filter_level0 = P9_LOOP_FILTER_LEVEL_0,
                                               int loop_filter_level1 = P9_LOOP_FILTER_LEVEL_1) {
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
    write_loop_filter_params(bw, loop_filter_level0, loop_filter_level1);
    bw.write_bit(0);
    bw.write_bit(0);
    bw.flush_zero_pad();
    return wrap_obu_fixed_leb128(0x32, bw.bytes, 4);
}

static std::vector<uint8_t> build_expected_inter(uint8_t qindex,
                                                 int loop_filter_level0 = P9_LOOP_FILTER_LEVEL_0,
                                                 int loop_filter_level1 = P9_LOOP_FILTER_LEVEL_1,
                                                 int is_motion_mode_switchable = 0) {
    BitWriter bw;
    bw.write_bit(0);
    bw.write_bits(1, 2);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(1);
    bw.write_bit(0);
    bw.write_bits(0x01, 8);
    for (int ref = 0; ref < 7; ++ref) bw.write_bits(0, 3);
    bw.write_bit(0);
    // force_integer_mv=1, so allow_high_precision_mv is not signaled.
    bw.write_bit(0);
    bw.write_bits(0, 2);
    bw.write_bit(is_motion_mode_switchable);
    write_tile_info(bw);
    write_quantization_params(bw, qindex);
    bw.write_bit(0);
    bw.write_bit(0);
    write_loop_filter_params(bw, loop_filter_level0, loop_filter_level1);
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
    dut.refresh_frame_flags_in = is_keyframe ? 0xFF : 0x01;
    dut.ref_frame_idx_map_in = 0;
    dut.frame_num = 0;
    dut.eval();

    for (int i = 0; i < 4; ++i) tick(&dut, out);
    dut.rst_n = 1;
    tick(&dut, out);

    dut.write_seq_hdr = seq_hdr ? 1 : 0;
    dut.write_frame_hdr = frame_hdr ? 1 : 0;
    dut.is_keyframe = is_keyframe ? 1 : 0;
    dut.qindex = qindex;
    dut.refresh_frame_flags_in = is_keyframe ? 0xFF : 0x01;
    dut.ref_frame_idx_map_in = 0;
    tick(&dut, out);
    dut.write_seq_hdr = 0;
    dut.write_frame_hdr = 0;

    for (int guard = 0; guard < 256; ++guard) {
        tick(&dut, out);
        if (dut.done) break;
    }
    return out;
}

static bool parse_sequence_disabled_filter_policy(const std::vector<uint8_t>& obu,
                                                  std::string* err) {
    std::vector<uint8_t> payload;
    if (!extract_payload_one_byte(obu, 0x0A, payload, err)) return false;
    BitReader br(payload);
    const uint32_t w_bits = static_cast<uint32_t>(bits_needed(FRAME_WIDTH));
    const uint32_t h_bits = static_cast<uint32_t>(bits_needed(FRAME_HEIGHT));
    if (!expect_bits(br, 0, 3, "seq_profile", err)) return false;
    if (!expect_bit(br, 0, "still_picture", err)) return false;
    if (!expect_bit(br, 0, "reduced_still_picture_header", err)) return false;
    if (!expect_bit(br, 0, "timing_info_present_flag", err)) return false;
    if (!expect_bit(br, 0, "initial_display_delay_present_flag", err)) return false;
    if (!expect_bits(br, 0, 5, "operating_points_cnt_minus_1", err)) return false;
    if (!expect_bits(br, 0, 12, "operating_point_idc", err)) return false;
    if (!expect_bits(br, 4, 5, "seq_level_idx", err)) return false;
    if (!expect_bits(br, w_bits - 1, 4, "frame_width_bits_minus_1", err)) return false;
    if (!expect_bits(br, h_bits - 1, 4, "frame_height_bits_minus_1", err)) return false;
    if (!expect_bits(br, FRAME_WIDTH - 1, static_cast<int>(w_bits), "max_frame_width_minus_1", err)) return false;
    if (!expect_bits(br, FRAME_HEIGHT - 1, static_cast<int>(h_bits), "max_frame_height_minus_1", err)) return false;
    if (!expect_bit(br, 0, "frame_id_numbers_present_flag", err)) return false;
    if (!expect_bit(br, 0, "use_128x128_superblock", err)) return false;
    if (!expect_bit(br, 0, "enable_filter_intra", err)) return false;
    if (!expect_bit(br, 0, "enable_intra_edge_filter", err)) return false;
    if (!expect_bit(br, 0, "enable_interintra_compound", err)) return false;
    if (!expect_bit(br, 0, "enable_masked_compound", err)) return false;
    if (!expect_bit(br, 0, "enable_warped_motion", err)) return false;
    if (!expect_bit(br, 0, "enable_dual_filter", err)) return false;
    if (!expect_bit(br, 0, "enable_order_hint", err)) return false;
    if (!expect_bit(br, 1, "seq_choose_screen_content_tools", err)) return false;
    if (!expect_bit(br, 1, "seq_choose_integer_mv", err)) return false;
    if (!expect_bit(br, 0, "enable_superres", err)) return false;
    if (!expect_bit(br, P9_ENABLE_CDEF, "enable_cdef", err)) return false;
    if (!expect_bit(br, P9_ENABLE_RESTORATION, "enable_restoration", err)) return false;
    return true;
}

static bool parse_frame_disabled_filter_policy(const std::vector<uint8_t>& obu,
                                               bool keyframe,
                                               uint8_t qindex,
                                               std::string* err) {
    std::vector<uint8_t> payload;
    if (!extract_payload_fixed_leb128(obu, 0x32, 4, payload, err)) return false;
    BitReader br(payload);
    if (!expect_bit(br, 0, "show_existing_frame", err)) return false;
    if (!expect_bits(br, keyframe ? 0 : 1, 2, "frame_type", err)) return false;
    if (!expect_bit(br, 1, "show_frame", err)) return false;
    if (!keyframe && !expect_bit(br, 1, "error_resilient_mode", err)) return false;
    if (!expect_bit(br, 1, "disable_cdf_update", err)) return false;
    if (keyframe) {
        if (!expect_bit(br, 0, "allow_screen_content_tools", err)) return false;
        if (!expect_bit(br, 0, "frame_size_override_flag", err)) return false;
    } else {
        if (!expect_bit(br, 1, "allow_screen_content_tools", err)) return false;
        if (!expect_bit(br, 1, "force_integer_mv", err)) return false;
        if (!expect_bit(br, 0, "frame_size_override_flag", err)) return false;
        if (!expect_bits(br, 0x01, 8, "refresh_frame_flags", err)) return false;
        for (int ref = 0; ref < 7; ++ref) {
            if (!expect_bits(br, 0, 3, "ref_order_hint", err)) return false;
        }
    }
    if (!expect_bit(br, 0, "render_and_frame_size_different", err)) return false;
    if (!keyframe) {
        // force_integer_mv=1, so allow_high_precision_mv is not signaled.
        if (!expect_bit(br, 0, "is_filter_switchable", err)) return false;
        if (!expect_bits(br, 0, 2, "interpolation_filter", err)) return false;
        if (!expect_bit(br, 0, "is_motion_mode_switchable", err)) return false;
    }
    if (!read_tile_info(br, err)) return false;
    if (!read_quantization_params(br, qindex, err)) return false;
    if (!expect_bit(br, 0, "segmentation_enabled", err)) return false;
    if (!expect_bit(br, 0, "delta_q_present", err)) return false;
    return read_disabled_loop_filter_params(br, err);
}

static bool expect_policy_accept(const char* label, bool accepted, const std::string& err) {
    if (accepted) {
        std::fprintf(stderr, "[PASS] %s\n", label);
        return true;
    }
    std::fprintf(stderr, "[FAIL] %s: %s\n", label, err.c_str());
    return false;
}

static bool expect_policy_reject(const char* label, bool accepted, const std::string& err) {
    if (!accepted) {
        std::fprintf(stderr, "[PASS] %s rejected forbidden policy: %s\n", label, err.c_str());
        return true;
    }
    std::fprintf(stderr, "[FAIL] %s accepted forbidden filter policy\n", label);
    return false;
}

static bool expect_ne(const char* label, const std::vector<uint8_t>& actual,
                      const std::vector<uint8_t>& forbidden) {
    if (actual != forbidden) {
        std::fprintf(stderr, "[PASS] %s forbidden bytes do not match\n", label);
        return true;
    }
    std::fprintf(stderr, "[FAIL] %s matched forbidden filter-enabled bytes: %s\n",
                 label, hex_string(actual).c_str());
    return false;
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
    const uint8_t qindices[] = {0, 1, 2, 63, 120, 121, 128, 240, 255};
    const std::string geom = std::to_string(FRAME_WIDTH) + "x" + std::to_string(FRAME_HEIGHT);
    const uint8_t qindex = 128;

    const auto seq_header = run_command(true, false, true, qindex);
    const auto key_header = run_command(false, true, true, qindex);
    const auto inter_header = run_command(false, true, false, qindex);

    bool ok = true;
    const std::string seq_label = "sequence_header " + geom;
    ok &= expect_eq(seq_label.c_str(),
                    seq_header,
                    build_expected_seq());

    for (uint8_t sweep_qindex : qindices) {
        const std::string qlabel = geom + " qindex=" + std::to_string(static_cast<unsigned>(sweep_qindex));
        const std::string key_label = "video_keyframe_header " + qlabel;
        const std::string inter_label = "video_inter_header " + qlabel;
        ok &= expect_eq(key_label.c_str(),
                        run_command(false, true, true, sweep_qindex),
                        build_expected_key(sweep_qindex));
        ok &= expect_eq(inter_label.c_str(),
                        run_command(false, true, false, sweep_qindex),
                        build_expected_inter(sweep_qindex));
    }

    std::string err;
    err.clear();
    ok &= expect_policy_accept("sequence_header_disabled_filter_policy",
                               parse_sequence_disabled_filter_policy(seq_header, &err), err);
    err.clear();
    ok &= expect_policy_accept("video_keyframe_disabled_loop_filter_policy",
                               parse_frame_disabled_filter_policy(key_header, true, qindex, &err), err);
    err.clear();
    ok &= expect_policy_accept("video_inter_disabled_loop_filter_policy",
                               parse_frame_disabled_filter_policy(inter_header, false, qindex, &err), err);

    const auto seq_with_cdef = build_expected_seq(1, P9_ENABLE_RESTORATION);
    err.clear();
    ok &= expect_policy_reject("guard_sequence_enable_cdef_parser",
                               parse_sequence_disabled_filter_policy(seq_with_cdef, &err), err);
    ok &= expect_ne("guard_sequence_enable_cdef_bytes", seq_header, seq_with_cdef);

    const auto seq_with_restoration = build_expected_seq(P9_ENABLE_CDEF, 1);
    err.clear();
    ok &= expect_policy_reject("guard_sequence_enable_restoration_parser",
                               parse_sequence_disabled_filter_policy(seq_with_restoration, &err), err);
    ok &= expect_ne("guard_sequence_enable_restoration_bytes", seq_header, seq_with_restoration);

    const auto key_with_filter = build_expected_key(qindex, 1, P9_LOOP_FILTER_LEVEL_1);
    err.clear();
    ok &= expect_policy_reject("guard_key_nonzero_loop_filter_parser",
                               parse_frame_disabled_filter_policy(key_with_filter, true, qindex, &err), err);
    ok &= expect_ne("guard_key_nonzero_loop_filter_bytes", key_header, key_with_filter);

    const auto inter_with_filter = build_expected_inter(qindex, P9_LOOP_FILTER_LEVEL_0, 1);
    err.clear();
    ok &= expect_policy_reject("guard_inter_nonzero_loop_filter_parser",
                               parse_frame_disabled_filter_policy(inter_with_filter, false, qindex, &err), err);
    ok &= expect_ne("guard_inter_nonzero_loop_filter_bytes", inter_header, inter_with_filter);

    const auto inter_with_switchable_motion = build_expected_inter(
        qindex, P9_LOOP_FILTER_LEVEL_0, P9_LOOP_FILTER_LEVEL_1, 1);
    err.clear();
    ok &= expect_policy_reject("guard_inter_switchable_motion_mode_parser",
                               parse_frame_disabled_filter_policy(inter_with_switchable_motion, false, qindex, &err), err);
    ok &= expect_ne("guard_inter_switchable_motion_mode_bytes", inter_header, inter_with_switchable_motion);

    if (ok) {
        std::fprintf(stderr,
                     "[PASS] header/qindex sweep geometry=%s qindices=%zu (standalone qindex=0 remains explicit; top-level lossless clamp is tested separately)\n",
                     geom.c_str(), sizeof(qindices) / sizeof(qindices[0]));
        std::fprintf(stderr,
                     "[PASS] disabled-filter policy guards geometry=%s qindex=%u\n",
                     geom.c_str(), static_cast<unsigned>(qindex));
    }

    return ok ? 0 : 1;
}
