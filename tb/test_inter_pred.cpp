#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "Vav1_inter_pred.h"
#include "verilated.h"

namespace {
constexpr int W = 32;
constexpr int H = 32;
constexpr int FILTER_BITS = 7;
constexpr int ROUND_OFFSET = 1 << (FILTER_BITS - 1);
constexpr int INTER_ROUND0_OFFSET = 1 << (3 - 1);
constexpr int INTER_ROUND1_IDENTITY_OFFSET = 1 << (4 - 1);

const int kRegularSubpel[16][8] = {
    { 0,  0,   0, 128,   0,   0,  0, 0},
    { 0,  2,  -6, 126,   8,  -2,  0, 0},
    { 0,  2, -10, 122,  18,  -4,  0, 0},
    { 0,  2, -12, 116,  28,  -8,  2, 0},
    { 0,  2, -14, 110,  38, -10,  2, 0},
    { 0,  2, -14, 102,  48, -12,  2, 0},
    { 0,  2, -16,  94,  58, -12,  2, 0},
    { 0,  2, -14,  84,  66, -12,  2, 0},
    { 0,  2, -14,  76,  76, -14,  2, 0},
    { 0,  2, -12,  66,  84, -14,  2, 0},
    { 0,  2, -12,  58,  94, -16,  2, 0},
    { 0,  2, -12,  48, 102, -14,  2, 0},
    { 0,  2, -10,  38, 110, -14,  2, 0},
    { 0,  2,  -8,  28, 116, -12,  2, 0},
    { 0,  0,  -4,  18, 122, -10,  2, 0},
    { 0,  0,  -2,   8, 126,  -6,  2, 0},
};

int clip8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

uint8_t sample_ref(const std::vector<uint8_t>& ref, int x, int y) {
    x = clamp(x, 0, W - 1);
    y = clamp(y, 0, H - 1);
    return ref[y * W + x];
}

uint8_t expected_pred(const std::vector<uint8_t>& ref, int base_x, int base_y,
                             int mv_x_q3, int mv_y_q3, int px, int py) {
    const int int_x = base_x + (mv_x_q3 >> 3) + px;
    const int int_y = base_y + (mv_y_q3 >> 3) + py;
    const int frac_x = mv_x_q3 & 7;
    const int frac_y = mv_y_q3 & 7;
    if (frac_x == 0 && frac_y == 0) {
        return sample_ref(ref, int_x, int_y);
    }
    if (frac_x != 0 && frac_y == 0) {
        const int phase = frac_x << 1;
        int sum = 0;
        for (int k = 0; k < 8; ++k) {
            sum += kRegularSubpel[phase][k] * sample_ref(ref, int_x + k - 3, int_y);
        }
        return static_cast<uint8_t>(clip8((((sum + INTER_ROUND0_OFFSET) >> 3) + INTER_ROUND1_IDENTITY_OFFSET) >> 4));
    }
    if (frac_x == 0 && frac_y != 0) {
        const int phase = frac_y << 1;
        int sum = 0;
        for (int k = 0; k < 8; ++k) {
            sum += kRegularSubpel[phase][k] * sample_ref(ref, int_x, int_y + k - 3);
        }
        return static_cast<uint8_t>(clip8((sum + ROUND_OFFSET) >> FILTER_BITS));
    }
    const int phase_x = frac_x << 1;
    const int phase_y = frac_y << 1;
    int vsum = 0;
    for (int ky = 0; ky < 8; ++ky) {
        int hsum = 0;
        for (int kx = 0; kx < 8; ++kx) {
            hsum += kRegularSubpel[phase_x][kx] * sample_ref(ref, int_x + kx - 3, int_y + ky - 3);
        }
        const int hrounded = (hsum + 4) >> 3;
        vsum += kRegularSubpel[phase_y][ky] * hrounded;
    }
    return static_cast<uint8_t>(clip8((vsum + 1024) >> 11));
}

void tick(Vav1_inter_pred& dut, vluint64_t& t) {
    dut.clk = 0; dut.eval(); ++t;
    dut.clk = 1; dut.eval(); ++t;
}

bool run_case(const std::string& name, int cur_x, int cur_y, int mv_x_q3, int mv_y_q3,
              const std::vector<uint8_t>& ref) {
    Vav1_inter_pred dut;
    vluint64_t t = 0;
    dut.rst_n = 0;
    dut.start = 0;
    dut.cur_x = cur_x;
    dut.cur_y = cur_y;
    dut.mv_x_q3 = mv_x_q3;
    dut.mv_y_q3 = mv_y_q3;
    dut.ref_mem_data = 0;
    for (int i = 0; i < 4; ++i) tick(dut, t);
    dut.rst_n = 1;
    tick(dut, t);
    dut.start = 1;
    tick(dut, t);
    dut.start = 0;

    for (int cyc = 0; cyc < 20000 && !dut.done; ++cyc) {
        const uint32_t addr = dut.ref_mem_addr;
        dut.ref_mem_data = addr < ref.size() ? ref[addr] : 0xEE;
        tick(dut, t);
    }
    if (!dut.done) {
        std::cerr << "[FAIL] " << name << ": timeout\n";
        return false;
    }
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        const int px = i & 7;
        const int py = i >> 3;
        const uint8_t got = dut.pred[i];
        const uint8_t exp = expected_pred(ref, cur_x, cur_y, mv_x_q3, mv_y_q3, px, py);
        if (got != exp) {
            std::cerr << "[FAIL] " << name << ": pred[" << i << "] got "
                      << static_cast<int>(got) << " expected " << static_cast<int>(exp) << "\n";
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
            ref[y * W + x] = static_cast<uint8_t>((11 * x + 7 * y + ((x * y) & 15)) & 0xFF);
        }
    }

    bool ok = true;
    ok = run_case("fullpel_positive_mv", 8, 9, 8, -8, ref) && ok;
    ok = run_case("horizontal_halfpel_regular_filter", 8, 9, 4, 0, ref) && ok;
    ok = run_case("vertical_halfpel_regular_filter", 8, 9, 0, 4, ref) && ok;
    ok = run_case("hv_halfpel_regular_filter", 8, 9, 4, 4, ref) && ok;
    return ok ? 0 : 1;
}
