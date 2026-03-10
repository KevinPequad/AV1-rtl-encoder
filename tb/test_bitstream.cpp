// test_bitstream.cpp — Standalone test for AV1 bitstream writer
// Generates a minimal AV1/IVF file with all-skip blocks, then one with coefficients.
// Compile: g++ -std=c++17 -o test_bitstream test_bitstream.cpp
// Run: ./test_bitstream && ffmpeg -y -i test_allskip.ivf -pix_fmt yuv420p test_decoded.yuv

#include "av1_bitstream_writer.h"
#include <fstream>
#include <cstdio>

static void write_file(const char* path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    fprintf(stderr, "Wrote %zu bytes to %s\n", data.size(), path);
}

static void emit_case(const char* label, const char* path,
                      const std::initializer_list<std::pair<int, int16_t>>& coeffs,
                      bool coeff_debug = false) {
    fprintf(stderr, "\n=== %s ===\n", label);
    const int w = 64, h = 64;
    AV1BitstreamWriter writer(w, h, 128);
    writer.set_coeff_debug_mode(coeff_debug);
    const int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        if (i == 0) {
            for (const auto& coeff : coeffs) bi.qcoeff[coeff.first] = coeff.second;
        }
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file(path, ivf);
}

static void emit_case_size(const char* label, const char* path, int w, int h,
                           const std::initializer_list<std::pair<int, int16_t>>& coeffs) {
    fprintf(stderr, "\n=== %s ===\n", label);
    AV1BitstreamWriter writer(w, h, 128);
    const int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        if (i == 0) {
            for (const auto& coeff : coeffs) bi.qcoeff[coeff.first] = coeff.second;
        }
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file(path, ivf);
}

// Test 1: All-skip frame (should produce flat gray)
static void test_allskip() {
    fprintf(stderr, "\n=== Test 1: All-skip 64x64 frame ===\n");
    int w = 64, h = 64;
    AV1BitstreamWriter writer(w, h, 128);
    int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file("test_allskip.ivf", ivf);
}

// Test 2: Single DC coefficient in first block
static void test_single_dc() {
    emit_case("Test 2: Single DC coeff 64x64 frame", "test_single_dc.ivf", {
        {0, 5},
    });
    emit_case("Test 2a: Single DC=10 coeff 64x64 frame", "test_single_dc10.ivf", {
        {0, 10},
    }, true);
    emit_case("Test 2b: Single DC=15 coeff 64x64 frame", "test_single_dc15.ivf", {
        {0, 15},
    });
}

// Test 3: A few coefficients in first block
static void test_few_coeffs() {
    emit_case("Test 3: Few coeffs 64x64 frame", "test_few_coeffs.ivf", {
        {0, 10},
        {1, -3},
        {8, 2},
    });
}

static void test_single_ac_cases() {
    emit_case("Test 3a: DC + AC@1 level1", "test_dc_ac1_pos1.ivf", {
        {0, 10},
        {1, 1},
    }, true);
    emit_case("Test 3b: DC + AC@8 level1", "test_dc_ac1_pos8.ivf", {
        {0, 10},
        {8, 1},
    });
    emit_case("Test 3c: DC + AC@1 level3", "test_dc_ac3_pos1.ivf", {
        {0, 10},
        {1, 3},
    });
    emit_case("Test 3d: DC + AC@8 level3", "test_dc_ac3_pos8.ivf", {
        {0, 10},
        {8, 3},
    });
    emit_case("Test 3e: Real block0 shape", "test_real_block0.ivf", {
        {0, 15},
        {8, -1},
    });
    emit_case_size("Test 3f: 8x8 DC=10", "test_8x8_dc10.ivf", 8, 8, {
        {0, 10},
    });
    emit_case_size("Test 3g: 8x8 DC=10 + AC@1", "test_8x8_dc10_ac1.ivf", 8, 8, {
        {0, 10},
        {1, 1},
    });
}

// Test 4: First block has skip=0 but all txb_skip=1 (should look like skip=1)
// This tests whether the block-level skip=0 with empty TXBs works
static void test_skip0_empty_txb() {
    fprintf(stderr, "\n=== Test 4: skip=0 but all-zero txb 64x64 frame ===\n");
    // To force skip=0, we need at least one non-zero coeff
    // But actually, encode_block checks qcoeff for non-zero to decide skip
    // So if all qcoeff are zero, skip=1 is encoded and no txb data follows
    // Let me instead directly test: what if qcoeff[0]=1 (minimal DC)
    int w = 64, h = 64;
    AV1BitstreamWriter writer(w, h, 128);
    int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        if (i == 0) {
            bi.qcoeff[0] = 1;  // minimal DC=1
        }
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file("test_dc1.ivf", ivf);
}

// Test 5a: Force skip=0 with all-zero coefficients (skip=0 + txb_skip=1 for all)
static void test_force_skip0() {
    fprintf(stderr, "\n=== Test 5a: Force skip=0 with all-zero txbs ===\n");
    int w = 64, h = 64;
    AV1BitstreamWriter writer(w, h, 128);
    writer.set_force_skip0(true);
    int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file("test_force_skip0.ivf", ivf);
}

// Test 5: 256x256 allskip (different tile_info than 64x64)
static void test_256_allskip() {
    fprintf(stderr, "\n=== Test 5: All-skip 256x256 frame ===\n");
    int w = 256, h = 256;
    AV1BitstreamWriter writer(w, h, 128);
    int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file("test_256_allskip.ivf", ivf);
}

// Test 6: 256x256 with DC=1 in first block
static void test_256_dc1() {
    fprintf(stderr, "\n=== Test 6: DC=1 256x256 frame ===\n");
    int w = 256, h = 256;
    AV1BitstreamWriter writer(w, h, 128);
    int blk_cols = w / 8, blk_rows = h / 8;
    for (int i = 0; i < blk_cols * blk_rows; i++) {
        AV1BitstreamWriter::BlockInfo bi = {};
        memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
        if (i == 0) bi.qcoeff[0] = 1;
        bi.pred_mode = 0;
        bi.is_inter = false;
        writer.add_block(bi);
    }
    auto ivf = writer.write_ivf_frame();
    write_file("test_256_dc1.ivf", ivf);
}

int main() {
    test_allskip();
    test_single_dc();
    test_few_coeffs();
    test_single_ac_cases();
    test_skip0_empty_txb();
    test_force_skip0();
    test_256_allskip();
    test_256_dc1();

    fprintf(stderr, "\nDone. Now try decoding:\n");
    return 0;
}
