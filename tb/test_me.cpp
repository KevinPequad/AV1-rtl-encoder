#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "Vav1_me.h"
#include "verilated.h"

namespace {
constexpr int W = FRAME_W;
constexpr int H = FRAME_H;
constexpr int FILTER_BITS = 7;
constexpr int ROUND_OFFSET = 1 << (FILTER_BITS - 1);

const int kRegularSubpel[16][8] = {
    { 0,  0,   0, 128,   0,   0,  0, 0}, { 0,  2,  -6, 126,   8,  -2,  0, 0},
    { 0,  2, -10, 122,  18,  -4,  0, 0}, { 0,  2, -12, 116,  28,  -8,  2, 0},
    { 0,  2, -14, 110,  38, -10,  2, 0}, { 0,  2, -14, 102,  48, -12,  2, 0},
    { 0,  2, -16,  94,  58, -12,  2, 0}, { 0,  2, -14,  84,  66, -12,  2, 0},
    { 0,  2, -14,  76,  76, -14,  2, 0}, { 0,  2, -12,  66,  84, -14,  2, 0},
    { 0,  2, -12,  58,  94, -16,  2, 0}, { 0,  2, -12,  48, 102, -14,  2, 0},
    { 0,  2, -10,  38, 110, -14,  2, 0}, { 0,  2,  -8,  28, 116, -12,  2, 0},
    { 0,  0,  -4,  18, 122, -10,  2, 0}, { 0,  0,  -2,   8, 126,  -6,  2, 0},
};

int clip8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
uint8_t sample(const std::vector<uint8_t>& ref, int x, int y) {
    if (x < 0) x = 0; if (x >= W) x = W - 1;
    if (y < 0) y = 0; if (y >= H) y = H - 1;
    return ref[y * W + x];
}
uint8_t hpel_x(const std::vector<uint8_t>& ref, int x, int y) {
    int sum = 0;
    for (int k = 0; k < 8; ++k) sum += kRegularSubpel[8][k] * sample(ref, x + k - 3, y);
    return static_cast<uint8_t>(clip8((sum + ROUND_OFFSET) >> FILTER_BITS));
}

uint8_t hpel_y(const std::vector<uint8_t>& ref, int x, int y) {
    int sum = 0;
    for (int k = 0; k < 8; ++k) sum += kRegularSubpel[8][k] * sample(ref, x, y + k - 3);
    return static_cast<uint8_t>(clip8((sum + ROUND_OFFSET) >> FILTER_BITS));
}
uint8_t hpel_xy(const std::vector<uint8_t>& ref, int x, int y) {
    int vsum = 0;
    for (int ky = 0; ky < 8; ++ky) {
        int hsum = 0;
        for (int kx = 0; kx < 8; ++kx)
            hsum += kRegularSubpel[8][kx] * sample(ref, x + kx - 3, y + ky - 3);
        const int hrounded = (hsum + 4) >> 3;
        vsum += kRegularSubpel[8][ky] * hrounded;
    }
    return static_cast<uint8_t>(clip8((vsum + 1024) >> 11));
}
void tick(Vav1_me& dut) {
    dut.clk = 0; dut.eval();
    dut.clk = 1; dut.eval();
}

bool run_halfpel_refine_case(const char* name, int expected_mvx_q3, int expected_mvy_q3) {
    std::vector<uint8_t> ref(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            ref[y * W + x] = static_cast<uint8_t>((5 * x + 10 * y + ((x * y) & 31)) & 0xFF);

    Vav1_me dut;
    dut.rst_n = 0;
    dut.start = 0;
    dut.zero_mv_only = 0;
    dut.integer_mv_only = 0;
    dut.cur_x = 8;
    dut.cur_y = 8;
    dut.ref_mem_data = 0;
    for (int i = 0; i < 4; ++i) tick(dut);
    dut.rst_n = 1;

    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            if (expected_mvx_q3 == 4 && expected_mvy_q3 == 4)
                dut.cur_blk[py * 8 + px] = hpel_xy(ref, 8 + px, 8 + py);
            else if (expected_mvx_q3 == 4)
                dut.cur_blk[py * 8 + px] = hpel_x(ref, 8 + px, 8 + py);
            else if (expected_mvy_q3 == 4)
                dut.cur_blk[py * 8 + px] = hpel_y(ref, 8 + px, 8 + py);
            else
                dut.cur_blk[py * 8 + px] = sample(ref, 8 + px, 8 + py);
        }
    }

    dut.start = 1;
    tick(dut);
    dut.start = 0;
    for (int cyc = 0; cyc < 200000 && !dut.done; ++cyc) {
        const uint32_t addr = dut.ref_mem_addr;
        dut.ref_mem_data = addr < ref.size() ? ref[addr] : 0;
        tick(dut);
    }
    if (!dut.done) {
        std::cerr << "[FAIL] " << name << ": timeout\n";
        return false;
    }
    if (dut.best_mvx_q3 != expected_mvx_q3 || dut.best_mvy_q3 != expected_mvy_q3) {
        std::cerr << "[FAIL] " << name << ": q3 mv got ("
                  << static_cast<int>(dut.best_mvx_q3) << ","
                  << static_cast<int>(dut.best_mvy_q3) << ") expected ("
                  << expected_mvx_q3 << "," << expected_mvy_q3 << "), fullpel=("
                  << static_cast<int>(dut.best_mvx) << "," << static_cast<int>(dut.best_mvy)
                  << ") sad=" << static_cast<int>(dut.best_sad) << "\n";
        return false;
    }
    std::cout << "[PASS] " << name << "\n";
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    bool ok = true;
    ok = run_halfpel_refine_case("horizontal_halfpel_refine", 4, 0) && ok;
    ok = run_halfpel_refine_case("hv_halfpel_refine", 4, 4) && ok;
    return ok ? 0 : 1;
}
