#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Vav1_chroma_residual.h"
#include "verilated.h"

namespace {
constexpr int QINDEX = 128;
constexpr int DC_DEQUANT_128 = 140;
constexpr int AC_DEQUANT_128 = 176;
constexpr int COS_BIT_FWD = 13;
constexpr int COS_BIT_INV = 12;
constexpr int F_COSPI_16 = 7568;
constexpr int F_COSPI_32 = 5793;
constexpr int F_COSPI_48 = 3135;
constexpr int I_COSPI_16 = 3784;
constexpr int I_COSPI_32 = 2896;
constexpr int I_COSPI_48 = 1567;

int half_btf(int w0, int a, int w1, int b, int cos_bit) {
    const int64_t prod = static_cast<int64_t>(w0) * a + static_cast<int64_t>(w1) * b;
    return static_cast<int>((prod + (1LL << (cos_bit - 1))) >> cos_bit);
}

std::array<int, 4> fdct4(const std::array<int, 4>& in) {
    std::array<int, 4> bf{};
    std::array<int, 4> st{};
    bf[0] = in[0] + in[3];
    bf[1] = in[1] + in[2];
    bf[2] = in[1] - in[2];
    bf[3] = in[0] - in[3];
    st[0] = half_btf( F_COSPI_32, bf[0],  F_COSPI_32, bf[1], COS_BIT_FWD);
    st[1] = half_btf(-F_COSPI_32, bf[1],  F_COSPI_32, bf[0], COS_BIT_FWD);
    st[2] = half_btf( F_COSPI_48, bf[2],  F_COSPI_16, bf[3], COS_BIT_FWD);
    st[3] = half_btf( F_COSPI_48, bf[3], -F_COSPI_16, bf[2], COS_BIT_FWD);
    return {st[0], st[2], st[1], st[3]};
}

std::array<int, 4> idct4(const std::array<int, 4>& in) {
    std::array<int, 4> bf{in[0], in[2], in[1], in[3]};
    std::array<int, 4> st{};
    st[0] = half_btf( I_COSPI_32, bf[0], I_COSPI_32, bf[1], COS_BIT_INV);
    st[1] = half_btf( I_COSPI_32, bf[0],-I_COSPI_32, bf[1], COS_BIT_INV);
    st[2] = half_btf( I_COSPI_48, bf[2],-I_COSPI_16, bf[3], COS_BIT_INV);
    st[3] = half_btf( I_COSPI_16, bf[2], I_COSPI_48, bf[3], COS_BIT_INV);
    return {st[0] + st[3], st[1] + st[2], st[1] - st[2], st[0] - st[3]};
}

int quantize(int coeff, bool is_dc) {
    const int dequant = is_dc ? DC_DEQUANT_128 : AC_DEQUANT_128;
    const int mag = std::abs(coeff);
    const int q = (mag + (dequant >> 1)) / dequant;
    return coeff < 0 ? -q : q;
}

int round_shift(int v, int shift) {
    return (v + (1 << (shift - 1))) >> shift;
}

uint8_t clip8(int v) {
    return static_cast<uint8_t>(std::max(0, std::min(255, v)));
}

struct ModelOut {
    std::array<int, 16> qcoeff{};
    std::array<uint8_t, 16> recon{};
    bool has_coeff = false;
};

ModelOut model(const std::array<uint8_t, 16>& cur,
               const std::array<uint8_t, 16>& pred,
               bool dc_only) {
    std::array<int, 16> residual{};
    std::array<int, 16> tmp{};
    std::array<int, 16> coeff{};
    std::array<int, 16> dq{};
    std::array<int, 16> inv_tmp{};
    ModelOut out;

    for (int i = 0; i < 16; ++i) residual[i] = static_cast<int>(cur[i]) - static_cast<int>(pred[i]);
    for (int y = 0; y < 4; ++y) {
        auto row = fdct4({residual[y * 4 + 0], residual[y * 4 + 1], residual[y * 4 + 2], residual[y * 4 + 3]});
        for (int x = 0; x < 4; ++x) tmp[y * 4 + x] = row[x];
    }
    for (int x = 0; x < 4; ++x) {
        auto col = fdct4({tmp[0 * 4 + x], tmp[1 * 4 + x], tmp[2 * 4 + x], tmp[3 * 4 + x]});
        for (int y = 0; y < 4; ++y) coeff[y * 4 + x] = col[y];
    }
    for (int i = 0; i < 16; ++i) {
        out.qcoeff[i] = (dc_only && i != 0) ? 0 : quantize(coeff[i], i == 0);
        if (out.qcoeff[i]) out.has_coeff = true;
        dq[i] = out.qcoeff[i] * (i == 0 ? DC_DEQUANT_128 : AC_DEQUANT_128);
    }
    for (int y = 0; y < 4; ++y) {
        auto row = idct4({dq[y * 4 + 0], dq[y * 4 + 1], dq[y * 4 + 2], dq[y * 4 + 3]});
        for (int x = 0; x < 4; ++x) inv_tmp[y * 4 + x] = round_shift(row[x], 1);
    }
    for (int x = 0; x < 4; ++x) {
        auto col = idct4({inv_tmp[0 * 4 + x], inv_tmp[1 * 4 + x], inv_tmp[2 * 4 + x], inv_tmp[3 * 4 + x]});
        for (int y = 0; y < 4; ++y) {
            const int idx = y * 4 + x;
            out.recon[idx] = clip8(static_cast<int>(pred[idx]) + round_shift(col[y], 3));
        }
    }
    return out;
}

void tick(Vav1_chroma_residual& dut) {
    dut.clk = 0; dut.eval();
    dut.clk = 1; dut.eval();
}

bool run_case(const std::string& name,
              const std::array<uint8_t, 16>& cur,
              const std::array<uint8_t, 16>& pred,
              bool dc_only) {
    const ModelOut exp = model(cur, pred, dc_only);
    Vav1_chroma_residual dut;
    dut.rst_n = 0;
    dut.start = 0;
    dut.qindex = QINDEX;
    dut.dc_only = dc_only ? 1 : 0;
    for (int i = 0; i < 16; ++i) {
        dut.cur[i] = cur[i];
        dut.pred[i] = pred[i];
    }
    for (int i = 0; i < 4; ++i) tick(dut);
    dut.rst_n = 1;
    tick(dut);
    dut.start = 1;
    tick(dut);
    dut.start = 0;
    for (int cyc = 0; cyc < 20000 && !dut.done; ++cyc) tick(dut);
    if (!dut.done) {
        std::cerr << "[FAIL] " << name << ": timeout\n";
        return false;
    }
    bool ok = true;
    if (!!dut.block_has_coeff != exp.has_coeff) {
        std::cerr << "[FAIL] " << name << ": block_has_coeff got " << (int)dut.block_has_coeff
                  << " expected " << exp.has_coeff << "\n";
        ok = false;
    }
    for (int i = 0; i < 16; ++i) {
        const int got_q = static_cast<int16_t>(dut.qcoeff[i]);
        if (got_q != exp.qcoeff[i]) {
            std::cerr << "[FAIL] " << name << ": qcoeff[" << i << "] got " << got_q
                      << " expected " << exp.qcoeff[i] << "\n";
            ok = false;
        }
        const uint8_t got_r = dut.recon[i];
        if (got_r != exp.recon[i]) {
            std::cerr << "[FAIL] " << name << ": recon[" << i << "] got " << static_cast<int>(got_r)
                      << " expected " << static_cast<int>(exp.recon[i]) << "\n";
            ok = false;
        }
    }
    if (ok) std::cout << "[PASS] " << name << "\n";
    return ok;
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    bool ok = true;
    std::array<uint8_t, 16> pred{};
    pred.fill(128);
    std::array<uint8_t, 16> flat{};
    flat.fill(160);
    ok = run_case("chroma_dc_only_flat_offset", flat, pred, true) && ok;

    std::array<uint8_t, 16> grad{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            grad[y * 4 + x] = static_cast<uint8_t>(120 + 9 * x + 5 * y);
    ok = run_case("chroma_full_4x4_gradient", grad, pred, false) && ok;

    std::array<uint8_t, 16> same{};
    same.fill(128);
    ok = run_case("chroma_zero_residual", same, pred, false) && ok;
    return ok ? 0 : 1;
}
