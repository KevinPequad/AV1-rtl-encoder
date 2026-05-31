#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "Vav1_chroma_inter_pred.h"
#include "verilated.h"

namespace {
#ifndef FRAME_W
#define FRAME_W 32
#endif
#ifndef FRAME_H
#define FRAME_H 32
#endif
constexpr int W = FRAME_W / 2;
constexpr int H = FRAME_H / 2;
static_assert(W >= 16 && H >= 16, "chroma inter pred test expects at least a 16x16 chroma plane");
constexpr int FILTER_BITS = 7;
constexpr int ROUND_OFFSET = 1 << (FILTER_BITS - 1);

// Small-block AV1 regular filters for width/height <= 4, scaled to sum 128.
const int kSmallRegularSubpel[16][8] = {
    { 0, 0,   0, 128,   0,   0, 0, 0},
    { 0, 0,  -4, 126,   8,  -2, 0, 0},
    { 0, 0,  -8, 122,  18,  -4, 0, 0},
    { 0, 0, -10, 116,  28,  -6, 0, 0},
    { 0, 0, -12, 110,  38,  -8, 0, 0},
    { 0, 0, -12, 102,  48, -10, 0, 0},
    { 0, 0, -14,  94,  58, -10, 0, 0},
    { 0, 0, -12,  84,  66, -10, 0, 0},
    { 0, 0, -12,  76,  76, -12, 0, 0},
    { 0, 0, -10,  66,  84, -12, 0, 0},
    { 0, 0, -10,  58,  94, -14, 0, 0},
    { 0, 0, -10,  48, 102, -12, 0, 0},
    { 0, 0,  -8,  38, 110, -12, 0, 0},
    { 0, 0,  -6,  28, 116, -10, 0, 0},
    { 0, 0,  -4,  18, 122,  -8, 0, 0},
    { 0, 0,  -2,   8, 126,  -4, 0, 0},
};

int clip8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint8_t sample(const std::vector<uint8_t>& ref, int x, int y) {
    x = clamp(x, 0, W - 1);
    y = clamp(y, 0, H - 1);
    return ref[y * W + x];
}

uint8_t expected_pred(const std::vector<uint8_t>& ref, int cur_x, int cur_y,
                      int mv_x_q3, int mv_y_q3, int px, int py) {
    const int int_x = cur_x + (mv_x_q3 >> 4) + px;
    const int int_y = cur_y + (mv_y_q3 >> 4) + py;
    const int frac_x = mv_x_q3 & 15;
    const int frac_y = mv_y_q3 & 15;
    if (frac_x == 0 && frac_y == 0) return sample(ref, int_x, int_y);
    if (frac_x != 0 && frac_y == 0) {
        int sum = 0;
        for (int k = 0; k < 8; ++k)
            sum += kSmallRegularSubpel[frac_x][k] * sample(ref, int_x + k - 3, int_y);
        return static_cast<uint8_t>(clip8((sum + ROUND_OFFSET) >> FILTER_BITS));
    }
    if (frac_x == 0 && frac_y != 0) {
        int sum = 0;
        for (int k = 0; k < 8; ++k)
            sum += kSmallRegularSubpel[frac_y][k] * sample(ref, int_x, int_y + k - 3);
        return static_cast<uint8_t>(clip8((sum + ROUND_OFFSET) >> FILTER_BITS));
    }
    int vsum = 0;
    for (int ky = 0; ky < 8; ++ky) {
        int hsum = 0;
        for (int kx = 0; kx < 8; ++kx)
            hsum += kSmallRegularSubpel[frac_x][kx] * sample(ref, int_x + kx - 3, int_y + ky - 3);
        const int hrounded = (hsum + 4) >> 3;
        vsum += kSmallRegularSubpel[frac_y][ky] * hrounded;
    }
    return static_cast<uint8_t>(clip8((vsum + 1024) >> 11));
}

void tick(Vav1_chroma_inter_pred& dut) {
    dut.clk = 0; dut.eval();
    dut.clk = 1; dut.eval();
}

uint8_t pred_byte(const Vav1_chroma_inter_pred& dut, int idx) {
    return static_cast<uint8_t>((dut.pred[idx >> 2] >> ((idx & 3) * 8)) & 0xFF);
}

bool run_case(const std::string& name, int cur_x, int cur_y, int mv_x_q3, int mv_y_q3,
              const std::vector<uint8_t>& ref,
              const std::vector<uint8_t>* expected_block = nullptr) {
    Vav1_chroma_inter_pred dut;
    dut.rst_n = 0;
    dut.start = 0;
    dut.cur_x = cur_x;
    dut.cur_y = cur_y;
    dut.mv_x_q3 = mv_x_q3;
    dut.mv_y_q3 = mv_y_q3;
    dut.ref_mem_data = 0;
    for (int i = 0; i < 4; ++i) tick(dut);
    dut.rst_n = 1;
    tick(dut);
    dut.start = 1;
    tick(dut);
    dut.start = 0;

    for (int cyc = 0; cyc < 20000 && !dut.done; ++cyc) {
        const uint32_t addr = dut.ref_mem_addr;
        dut.ref_mem_data = addr < ref.size() ? ref[addr] : 0xEE;
        tick(dut);
    }
    if (!dut.done) {
        std::cerr << "[FAIL] " << name << ": timeout\n";
        return false;
    }
    bool ok = true;
    if (expected_block && expected_block->size() != 16) {
        std::cerr << "[FAIL] " << name << ": expected signature must have 16 samples\n";
        ok = false;
    }
    for (int i = 0; i < 16; ++i) {
        const int px = i & 3;
        const int py = i >> 2;
        const uint8_t got = pred_byte(dut, i);
        const uint8_t exp = expected_pred(ref, cur_x, cur_y, mv_x_q3, mv_y_q3, px, py);
        if (got != exp) {
            std::cerr << "[FAIL] " << name << ": pred[" << i << "] got "
                      << static_cast<int>(got) << " expected " << static_cast<int>(exp) << "\n";
            ok = false;
        }
        if (expected_block && expected_block->size() == 16 &&
            (got != (*expected_block)[i] || exp != (*expected_block)[i])) {
            std::cerr << "[FAIL] " << name << ": pred[" << i << "] signature got "
                      << static_cast<int>(got) << " model " << static_cast<int>(exp)
                      << " expected_signature " << static_cast<int>((*expected_block)[i]) << "\n";
            ok = false;
        }
    }
    if (ok) std::cout << "[PASS] " << name << "\n";
    return ok;
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    std::vector<uint8_t> ref(W * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            ref[y * W + x] = static_cast<uint8_t>((13 * x + 17 * y + ((x * y) & 31)) & 0xFF);
        }
    }
    bool ok = true;
    ok = run_case("chroma_fullpel", 4, 5, 16, -16, ref) && ok;
    ok = run_case("chroma_quarterpel_x_from_luma_halfpel", 4, 5, 4, 0, ref) && ok;
    ok = run_case("chroma_quarterpel_y_from_luma_halfpel", 4, 5, 0, 4, ref) && ok;
    ok = run_case("chroma_2d_quarterpel_from_luma_halfpel", 4, 5, 4, 4, ref) && ok;
    ok = run_case("chroma_negative_fractional", 4, 5, -4, -4, ref) && ok;

    // Reproduce the narrow 64x64 3-frame NEWMV Cb blocker reference taps in a
    // standalone RTL check.  The row values are frame-1 Cb reference samples
    // x=7..17 for y=8..11; with blk33's mv=(104,-128), output sample
    // pred[13] must stay at phase-8 value 0xA3.  This keeps the standalone
    // chroma predictor aligned with the top-level diagnostic while public
    // decoders still reconstruct that one sample as 0xA4.
    std::vector<uint8_t> blocker_ref(W * H, 0);
    const uint8_t blocker_rows[4][11] = {
        {142, 142, 142, 144, 146, 162, 162, 162, 162, 162, 162},
        {141, 141, 141, 143, 145, 168, 168, 168, 168, 168, 168},
        {144, 147, 146, 149, 153, 167, 167, 167, 167, 167, 167},
        {146, 152, 151, 155, 160, 166, 166, 166, 166, 166, 166},
    };
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 11; ++x)
            blocker_ref[(8 + y) * W + 7 + x] = blocker_rows[y][x];
    const std::vector<uint8_t> blocker_expected = {
        144, 154, 164, 162,
        142, 157, 170, 168,
        150, 160, 168, 167,
        157, 163, 167, 166,
    };
    ok = run_case("chroma_frame2_cb_blocker_phase8_signature",
                  4, 16, 104, -128, blocker_ref, &blocker_expected) && ok;
    return ok ? 0 : 1;
}
