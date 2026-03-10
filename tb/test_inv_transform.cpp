#include "Vav1_inverse_transform.h"
#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

constexpr int COS_BIT = 12;
constexpr int32_t COSPI_8 = 4017;
constexpr int32_t COSPI_16 = 3784;
constexpr int32_t COSPI_24 = 3406;
constexpr int32_t COSPI_32 = 2896;
constexpr int32_t COSPI_40 = 2276;
constexpr int32_t COSPI_48 = 1567;
constexpr int32_t COSPI_56 = 799;

using Vec8 = std::array<int32_t, 8>;
using Block64 = std::array<int32_t, 64>;

int32_t half_btf_ref(int32_t w0, int32_t in0, int32_t w1, int32_t in1) {
    const int64_t prod = static_cast<int64_t>(w0) * in0 + static_cast<int64_t>(w1) * in1;
    return static_cast<int32_t>((prod + (1ll << (COS_BIT - 1))) >> COS_BIT);
}

int32_t round_shift_ref(int32_t value, int shift) {
    if (shift <= 0) return value;
    return (value + (1 << (shift - 1))) >> shift;
}

void idct8_ref(const Vec8& input, Vec8& output) {
    Vec8 step{};
    Vec8 bf0{};
    Vec8 bf1{};

    bf1[0] = input[0];
    bf1[1] = input[4];
    bf1[2] = input[2];
    bf1[3] = input[6];
    bf1[4] = input[1];
    bf1[5] = input[5];
    bf1[6] = input[3];
    bf1[7] = input[7];

    bf0 = bf1;
    bf1[0] = bf0[0];
    bf1[1] = bf0[1];
    bf1[2] = bf0[2];
    bf1[3] = bf0[3];
    bf1[4] = half_btf_ref(COSPI_56, bf0[4], -COSPI_8, bf0[7]);
    bf1[5] = half_btf_ref(COSPI_24, bf0[5], -COSPI_40, bf0[6]);
    bf1[6] = half_btf_ref(COSPI_40, bf0[5], COSPI_24, bf0[6]);
    bf1[7] = half_btf_ref(COSPI_8, bf0[4], COSPI_56, bf0[7]);

    bf0 = bf1;
    bf1[0] = half_btf_ref(COSPI_32, bf0[0], COSPI_32, bf0[1]);
    bf1[1] = half_btf_ref(COSPI_32, bf0[0], -COSPI_32, bf0[1]);
    bf1[2] = half_btf_ref(COSPI_48, bf0[2], -COSPI_16, bf0[3]);
    bf1[3] = half_btf_ref(COSPI_16, bf0[2], COSPI_48, bf0[3]);
    bf1[4] = bf0[4] + bf0[5];
    bf1[5] = bf0[4] - bf0[5];
    bf1[6] = -bf0[6] + bf0[7];
    bf1[7] = bf0[6] + bf0[7];

    bf0 = bf1;
    step[0] = bf0[0] + bf0[3];
    step[1] = bf0[1] + bf0[2];
    step[2] = bf0[1] - bf0[2];
    step[3] = bf0[0] - bf0[3];
    step[4] = bf0[4];
    step[5] = half_btf_ref(-COSPI_32, bf0[5], COSPI_32, bf0[6]);
    step[6] = half_btf_ref(COSPI_32, bf0[5], COSPI_32, bf0[6]);
    step[7] = bf0[7];

    output[0] = step[0] + step[7];
    output[1] = step[1] + step[6];
    output[2] = step[2] + step[5];
    output[3] = step[3] + step[4];
    output[4] = step[3] - step[4];
    output[5] = step[2] - step[5];
    output[6] = step[1] - step[6];
    output[7] = step[0] - step[7];
}

class InvTransformSim {
public:
    InvTransformSim() {
        dut_.clk = 0;
        dut_.rst_n = 0;
        dut_.start = 0;
        dut_.is_4x4 = 0;
        for (int i = 0; i < 4; ++i) tick();
        dut_.rst_n = 1;
        for (int i = 0; i < 2; ++i) tick();
    }

    std::array<int16_t, 8> run_idct8(const std::array<int16_t, 8>& in) {
        dut_.in0 = in[0];
        dut_.in1 = in[1];
        dut_.in2 = in[2];
        dut_.in3 = in[3];
        dut_.in4 = in[4];
        dut_.in5 = in[5];
        dut_.in6 = in[6];
        dut_.in7 = in[7];
        dut_.is_4x4 = 0;
        dut_.start = 1;
        tick();
        dut_.start = 0;

        for (int i = 0; i < 16; ++i) {
            tick();
            if (dut_.done) {
                return {static_cast<int16_t>(dut_.out0), static_cast<int16_t>(dut_.out1),
                        static_cast<int16_t>(dut_.out2), static_cast<int16_t>(dut_.out3),
                        static_cast<int16_t>(dut_.out4), static_cast<int16_t>(dut_.out5),
                        static_cast<int16_t>(dut_.out6), static_cast<int16_t>(dut_.out7)};
            }
        }

        std::fprintf(stderr, "[inv-xform] ERROR: timed out waiting for done\n");
        std::exit(1);
    }

private:
    void tick() {
        dut_.clk = 1;
        dut_.eval();
        dut_.clk = 0;
        dut_.eval();
    }

    Vav1_inverse_transform dut_;
};

Block64 ref_inv2d_8x8(const std::array<int16_t, 64>& coeffs) {
    Block64 temp{};
    Block64 out{};

    for (int row = 0; row < 8; ++row) {
        Vec8 in{};
        Vec8 row_out{};
        for (int col = 0; col < 8; ++col) in[col] = coeffs[row * 8 + col];
        idct8_ref(in, row_out);
        for (int col = 0; col < 8; ++col) temp[row * 8 + col] = round_shift_ref(row_out[col], 1);
    }

    for (int col = 0; col < 8; ++col) {
        Vec8 in{};
        Vec8 col_out{};
        for (int row = 0; row < 8; ++row) in[row] = temp[row * 8 + col];
        idct8_ref(in, col_out);
        for (int row = 0; row < 8; ++row) out[row * 8 + col] = round_shift_ref(col_out[row], 4);
    }

    return out;
}

Block64 rtl_inv2d_8x8(InvTransformSim& sim, const std::array<int16_t, 64>& coeffs) {
    Block64 work{};
    Block64 out{};

    for (int idx = 0; idx < 64; ++idx) {
        work[idx] = coeffs[idx];
    }

    for (int row = 0; row < 8; ++row) {
        std::array<int16_t, 8> in{};
        for (int col = 0; col < 8; ++col) in[col] = static_cast<int16_t>(work[row * 8 + col]);
        const auto rtl_out = sim.run_idct8(in);
        for (int col = 0; col < 8; ++col) work[row * 8 + col] = round_shift_ref(rtl_out[col], 1);
    }

    for (int col = 0; col < 8; ++col) {
        std::array<int16_t, 8> in{};
        for (int row = 0; row < 8; ++row) in[row] = static_cast<int16_t>(work[row * 8 + col]);
        const auto rtl_out = sim.run_idct8(in);
        for (int row = 0; row < 8; ++row) out[row * 8 + col] = round_shift_ref(rtl_out[row], 4);
    }

    return out;
}

void print_block(const char* tag, const Block64& block) {
    std::fprintf(stderr, "%s\n", tag);
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            std::fprintf(stderr, "%s%d", col ? "," : "", block[row * 8 + col]);
        }
        std::fprintf(stderr, "\n");
    }
}

bool check_block(InvTransformSim& sim, const std::array<int16_t, 64>& coeffs, const char* name) {
    const auto rtl = rtl_inv2d_8x8(sim, coeffs);
    const auto ref = ref_inv2d_8x8(coeffs);

    for (int i = 0; i < 64; ++i) {
        if (rtl[i] != ref[i]) {
            std::fprintf(stderr, "[inv-xform] MISMATCH on %s at idx=%d rtl=%d ref=%d\n",
                         name, i, rtl[i], ref[i]);
            print_block("[inv-xform] RTL", rtl);
            print_block("[inv-xform] REF", ref);
            return false;
        }
    }
    return true;
}

bool check_vector(InvTransformSim& sim, const std::array<int16_t, 8>& in, const char* name) {
    Vec8 ref{};
    idct8_ref({in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7]}, ref);
    const auto rtl = sim.run_idct8(in);
    for (int i = 0; i < 8; ++i) {
        if (rtl[i] != ref[i]) {
            std::fprintf(stderr, "[inv-xform] 1D MISMATCH on %s at idx=%d rtl=%d ref=%d\n",
                         name, i, rtl[i], ref[i]);
            std::fprintf(stderr, "[inv-xform] in=");
            for (int j = 0; j < 8; ++j)
                std::fprintf(stderr, "%s%d", j ? "," : "", in[j]);
            std::fprintf(stderr, "\n[inv-xform] rtl=");
            for (int j = 0; j < 8; ++j)
                std::fprintf(stderr, "%s%d", j ? "," : "", rtl[j]);
            std::fprintf(stderr, "\n[inv-xform] ref=");
            for (int j = 0; j < 8; ++j)
                std::fprintf(stderr, "%s%d", j ? "," : "", ref[j]);
            std::fprintf(stderr, "\n");
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    Verilated::traceEverOn(false);

    InvTransformSim sim;

    const std::array<int16_t, 64> q224_block3 = {
        -3,  2,  0, 0, 0, 0, 0, 0,
         1,  0, -1, 0, 0, 0, 0, 0,
         0, -1,  1, 1, 0, 0, 0, 0,
         0,  0,  0, 0, 0, 0, 0, 0,
         0,  0,  0, 0, 0, 0, -1, 0,
         0,  0,  0, 0, 0, 0, 0, 0,
         0,  0,  0, 0, 0, 0, 0, 0,
         0,  0,  0, 0, 0, 0, 0, 0
    };

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(-32, 32);

    for (int t = 0; t < 200; ++t) {
        std::array<int16_t, 8> in{};
        for (int i = 0; i < 8; ++i) in[i] = static_cast<int16_t>(dist(rng));
        if (!check_vector(sim, in, "random_1d")) return 1;
    }

    if (!check_block(sim, q224_block3, "q224_block3")) return 1;

    for (int t = 0; t < 200; ++t) {
        std::array<int16_t, 64> coeffs{};
        for (int i = 0; i < 64; ++i) coeffs[i] = static_cast<int16_t>(dist(rng));
        if (!check_block(sim, coeffs, "random")) return 1;
    }

    std::fprintf(stderr, "[inv-xform] PASS\n");
    return 0;
}
