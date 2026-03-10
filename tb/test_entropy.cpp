// test_entropy.cpp - Reference check for rtl/av1_entropy.v
//
// Builds the standalone av1_entropy RTL module with Verilator and compares
// its emitted byte stream against the C++ AV1RangeCoder already used by the
// software-side debug writer.

#include "Vav1_entropy.h"
#include "verilated.h"

#include "av1_bitstream_writer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Op {
    enum Kind {
        kBool,
        kLit,
        kSymbol,
    } kind;
    int value;
    int prob;
    int bits;
    int nsyms;
    std::vector<uint16_t> icdf;
};

void tick(Vav1_entropy& dut, std::vector<uint8_t>* out = nullptr) {
    dut.clk = 1;
    dut.eval();
    if (out && dut.byte_valid) out->push_back(static_cast<uint8_t>(dut.byte_out));
    dut.clk = 0;
    dut.eval();
}

void reset_dut(Vav1_entropy& dut) {
    dut.clk = 0;
    dut.rst_n = 0;
    dut.init = 0;
    dut.encode_bool = 0;
    dut.encode_lit = 0;
    dut.encode_symbol = 0;
    dut.finalize = 0;
    dut.bool_val = 0;
    dut.bool_prob = 0;
    dut.lit_val = 0;
    dut.lit_bits = 0;
    dut.symbol = 0;
    dut.nsyms = 0;
    for (int i = 0; i < 8; ++i) dut.icdf_flat[i] = 0;
    dut.eval();
    tick(dut);
    dut.rst_n = 1;
    tick(dut);
}

void wait_done(Vav1_entropy& dut, std::vector<uint8_t>* out = nullptr, int max_cycles = 200000) {
    for (int i = 0; i < max_cycles; ++i) {
        tick(dut, out);
        if (dut.done) return;
    }
    std::fprintf(stderr, "Timed out waiting for done\n");
    std::exit(1);
}

void pulse_init(Vav1_entropy& dut) {
    dut.init = 1;
    wait_done(dut);
    dut.init = 0;
}

void do_bool(Vav1_entropy& dut, int val, int prob_q15) {
    dut.bool_val = val ? 1 : 0;
    dut.bool_prob = prob_q15;
    dut.encode_bool = 1;
    wait_done(dut);
    dut.encode_bool = 0;
}

void do_lit(Vav1_entropy& dut, int val, int bits) {
    dut.lit_val = val & 0xFF;
    dut.lit_bits = bits;
    dut.encode_lit = 1;
    tick(dut);
    dut.encode_lit = 0;
    if (!dut.done) wait_done(dut);
}

void set_icdf_flat(Vav1_entropy& dut, const std::vector<uint16_t>& icdf) {
    for (int i = 0; i < 8; ++i) dut.icdf_flat[i] = 0;
    for (size_t i = 0; i < icdf.size(); ++i) {
        const size_t bit = i * 16;
        const size_t word = bit / 32;
        const size_t shift = bit % 32;
        dut.icdf_flat[word] |= static_cast<uint32_t>(icdf[i]) << shift;
    }
}

void do_symbol(Vav1_entropy& dut, int symbol, int nsyms, const std::vector<uint16_t>& icdf) {
    dut.symbol = symbol;
    dut.nsyms = nsyms;
    set_icdf_flat(dut, icdf);
    dut.encode_symbol = 1;
    wait_done(dut);
    dut.encode_symbol = 0;
}

std::vector<uint8_t> do_finalize(Vav1_entropy& dut) {
    std::vector<uint8_t> out;
    dut.finalize = 1;
    tick(dut, &out);
    dut.finalize = 0;
    if (!dut.done) wait_done(dut, &out);
    return out;
}

std::vector<uint8_t> run_ref(const std::vector<Op>& ops) {
    AV1RangeCoder rc;
    rc.init();
    for (const auto& op : ops) {
        if (op.kind == Op::kBool) rc.encode_bool(op.value, op.prob);
        else if (op.kind == Op::kLit) rc.encode_literal(static_cast<unsigned>(op.value), op.bits);
        else rc.encode_symbol(op.value, op.icdf.data(), op.nsyms);
    }
    return rc.finish();
}

std::vector<uint8_t> run_dut(const std::vector<Op>& ops) {
    Vav1_entropy dut;
    reset_dut(dut);
    pulse_init(dut);
    for (const auto& op : ops) {
        if (op.kind == Op::kBool) do_bool(dut, op.value, op.prob);
        else if (op.kind == Op::kLit) do_lit(dut, op.value, op.bits);
        else do_symbol(dut, op.value, op.nsyms, op.icdf);
    }
    auto out = do_finalize(dut);
    dut.final();
    return out;
}

void dump_bytes(const char* label, const std::vector<uint8_t>& data) {
    std::fprintf(stderr, "%s (%zu bytes):", label, data.size());
    for (uint8_t b : data) std::fprintf(stderr, " %02x", b);
    std::fprintf(stderr, "\n");
}

void run_case(const char* label, const std::vector<Op>& ops) {
    auto ref = run_ref(ops);
    auto rtl = run_dut(ops);
    if (ref != rtl) {
        std::fprintf(stderr, "Mismatch in case: %s\n", label);
        dump_bytes("ref", ref);
        dump_bytes("rtl", rtl);
        std::exit(1);
    }
    std::fprintf(stderr, "[OK] %s -> %zu bytes\n", label, rtl.size());
}

std::vector<Op> build_probe_16x16_static_case() {
    std::vector<Op> ops;
    auto push_symbol = [&](int value, int nsyms, std::initializer_list<uint16_t> icdf) {
        ops.push_back(Op{Op::kSymbol, value, 0, 0, nsyms, std::vector<uint16_t>(icdf)});
    };
    auto push_bool = [&](int value) {
        ops.push_back(Op{Op::kBool, value, 16384, 0, 0, {}});
    };

    // Partition split to 8x8 for the 16x16 root, then 8x8 PARTITION_NONE for
    // the four leaf blocks. This sequence matches the current RTL 16x16
    // ac_probe static-CDF keyframe bring-up.
    push_symbol(3, 10, {av1_partition_cdf[4][0], av1_partition_cdf[4][1], av1_partition_cdf[4][2],
                        av1_partition_cdf[4][3], av1_partition_cdf[4][4], av1_partition_cdf[4][5],
                        av1_partition_cdf[4][6], av1_partition_cdf[4][7], av1_partition_cdf[4][8],
                        av1_partition_cdf[4][9]});

    // Block (0,0): V_PRED, qcoeff = {-1, -1}
    push_symbol(0, 4, {av1_partition_cdf[0][0], av1_partition_cdf[0][1], av1_partition_cdf[0][2], av1_partition_cdf[0][3]});
    push_symbol(0, 2, {av1_skip_cdf[0][0], av1_skip_cdf[0][1]});
    push_symbol(1, 13, {av1_kf_y_mode_cdf[0][0][0], av1_kf_y_mode_cdf[0][0][1], av1_kf_y_mode_cdf[0][0][2],
                        av1_kf_y_mode_cdf[0][0][3], av1_kf_y_mode_cdf[0][0][4], av1_kf_y_mode_cdf[0][0][5],
                        av1_kf_y_mode_cdf[0][0][6], av1_kf_y_mode_cdf[0][0][7], av1_kf_y_mode_cdf[0][0][8],
                        av1_kf_y_mode_cdf[0][0][9], av1_kf_y_mode_cdf[0][0][10], av1_kf_y_mode_cdf[0][0][11],
                        av1_kf_y_mode_cdf[0][0][12]});
    push_symbol(3, 7, {av1_angle_delta_cdf[0][0], av1_angle_delta_cdf[0][1], av1_angle_delta_cdf[0][2],
                       av1_angle_delta_cdf[0][3], av1_angle_delta_cdf[0][4], av1_angle_delta_cdf[0][5],
                       av1_angle_delta_cdf[0][6]});
    push_symbol(0, 14, {av1_uv_mode_cdf_cfl[1][0], av1_uv_mode_cdf_cfl[1][1], av1_uv_mode_cdf_cfl[1][2],
                        av1_uv_mode_cdf_cfl[1][3], av1_uv_mode_cdf_cfl[1][4], av1_uv_mode_cdf_cfl[1][5],
                        av1_uv_mode_cdf_cfl[1][6], av1_uv_mode_cdf_cfl[1][7], av1_uv_mode_cdf_cfl[1][8],
                        av1_uv_mode_cdf_cfl[1][9], av1_uv_mode_cdf_cfl[1][10], av1_uv_mode_cdf_cfl[1][11],
                        av1_uv_mode_cdf_cfl[1][12], av1_uv_mode_cdf_cfl[1][13]});
    push_symbol(0, 2, {av1_txb_skip_cdf[0][0], av1_txb_skip_cdf[0][1]});
    push_symbol(1, 7, {av1_intra_tx_type_cdf_8x8[1][0], av1_intra_tx_type_cdf_8x8[1][1], av1_intra_tx_type_cdf_8x8[1][2],
                       av1_intra_tx_type_cdf_8x8[1][3], av1_intra_tx_type_cdf_8x8[1][4], av1_intra_tx_type_cdf_8x8[1][5],
                       av1_intra_tx_type_cdf_8x8[1][6]});
    push_symbol(2, 7, {av1_eob_multi64_cdf[0][0], av1_eob_multi64_cdf[0][1], av1_eob_multi64_cdf[0][2],
                       av1_eob_multi64_cdf[0][3], av1_eob_multi64_cdf[0][4], av1_eob_multi64_cdf[0][5],
                       av1_eob_multi64_cdf[0][6]});
    push_symbol(0, 2, {av1_eob_extra_cdf[0][0][0], av1_eob_extra_cdf[0][0][1]});
    push_symbol(0, 3, {av1_coeff_base_eob_cdf[1][0], av1_coeff_base_eob_cdf[1][1], av1_coeff_base_eob_cdf[1][2]});
    push_symbol(0, 4, {av1_coeff_base_cdf[1][0], av1_coeff_base_cdf[1][1], av1_coeff_base_cdf[1][2], av1_coeff_base_cdf[1][3]});
    push_symbol(1, 4, {av1_coeff_base_cdf[0][0], av1_coeff_base_cdf[0][1], av1_coeff_base_cdf[0][2], av1_coeff_base_cdf[0][3]});
    push_symbol(1, 2, {av1_dc_sign_cdf[0][0][0], av1_dc_sign_cdf[0][0][1]});
    push_symbol(1, 2, {av1_txb_skip_cdf_4x4[7][0], av1_txb_skip_cdf_4x4[7][1]});
    push_symbol(1, 2, {av1_txb_skip_cdf_4x4[7][0], av1_txb_skip_cdf_4x4[7][1]});

    // Block (1,0): D203_PRED, qcoeff = {2}
    push_symbol(0, 4, {av1_partition_cdf[0][0], av1_partition_cdf[0][1], av1_partition_cdf[0][2], av1_partition_cdf[0][3]});
    push_symbol(0, 2, {av1_skip_cdf[0][0], av1_skip_cdf[0][1]});
    push_symbol(7, 13, {av1_kf_y_mode_cdf[0][1][0], av1_kf_y_mode_cdf[0][1][1], av1_kf_y_mode_cdf[0][1][2],
                        av1_kf_y_mode_cdf[0][1][3], av1_kf_y_mode_cdf[0][1][4], av1_kf_y_mode_cdf[0][1][5],
                        av1_kf_y_mode_cdf[0][1][6], av1_kf_y_mode_cdf[0][1][7], av1_kf_y_mode_cdf[0][1][8],
                        av1_kf_y_mode_cdf[0][1][9], av1_kf_y_mode_cdf[0][1][10], av1_kf_y_mode_cdf[0][1][11],
                        av1_kf_y_mode_cdf[0][1][12]});
    push_symbol(3, 7, {av1_angle_delta_cdf[6][0], av1_angle_delta_cdf[6][1], av1_angle_delta_cdf[6][2],
                       av1_angle_delta_cdf[6][3], av1_angle_delta_cdf[6][4], av1_angle_delta_cdf[6][5],
                       av1_angle_delta_cdf[6][6]});
    push_symbol(0, 14, {av1_uv_mode_cdf_cfl[7][0], av1_uv_mode_cdf_cfl[7][1], av1_uv_mode_cdf_cfl[7][2],
                        av1_uv_mode_cdf_cfl[7][3], av1_uv_mode_cdf_cfl[7][4], av1_uv_mode_cdf_cfl[7][5],
                        av1_uv_mode_cdf_cfl[7][6], av1_uv_mode_cdf_cfl[7][7], av1_uv_mode_cdf_cfl[7][8],
                        av1_uv_mode_cdf_cfl[7][9], av1_uv_mode_cdf_cfl[7][10], av1_uv_mode_cdf_cfl[7][11],
                        av1_uv_mode_cdf_cfl[7][12], av1_uv_mode_cdf_cfl[7][13]});
    push_symbol(0, 2, {av1_txb_skip_cdf[0][0], av1_txb_skip_cdf[0][1]});
    push_symbol(1, 7, {av1_intra_tx_type_cdf_8x8[7][0], av1_intra_tx_type_cdf_8x8[7][1], av1_intra_tx_type_cdf_8x8[7][2],
                       av1_intra_tx_type_cdf_8x8[7][3], av1_intra_tx_type_cdf_8x8[7][4], av1_intra_tx_type_cdf_8x8[7][5],
                       av1_intra_tx_type_cdf_8x8[7][6]});
    push_symbol(0, 7, {av1_eob_multi64_cdf[0][0], av1_eob_multi64_cdf[0][1], av1_eob_multi64_cdf[0][2],
                       av1_eob_multi64_cdf[0][3], av1_eob_multi64_cdf[0][4], av1_eob_multi64_cdf[0][5],
                       av1_eob_multi64_cdf[0][6]});
    push_symbol(1, 3, {av1_coeff_base_eob_cdf[0][0], av1_coeff_base_eob_cdf[0][1], av1_coeff_base_eob_cdf[0][2]});
    push_symbol(0, 2, {av1_dc_sign_cdf[0][1][0], av1_dc_sign_cdf[0][1][1]});
    push_symbol(1, 2, {av1_txb_skip_cdf_4x4[7][0], av1_txb_skip_cdf_4x4[7][1]});
    push_symbol(1, 2, {av1_txb_skip_cdf_4x4[7][0], av1_txb_skip_cdf_4x4[7][1]});

    // Block (0,1): D45_PRED, all-zero coeffs.
    push_symbol(0, 4, {av1_partition_cdf[0][0], av1_partition_cdf[0][1], av1_partition_cdf[0][2], av1_partition_cdf[0][3]});
    push_symbol(1, 2, {av1_skip_cdf[0][0], av1_skip_cdf[0][1]});
    push_symbol(3, 13, {av1_kf_y_mode_cdf[1][0][0], av1_kf_y_mode_cdf[1][0][1], av1_kf_y_mode_cdf[1][0][2],
                        av1_kf_y_mode_cdf[1][0][3], av1_kf_y_mode_cdf[1][0][4], av1_kf_y_mode_cdf[1][0][5],
                        av1_kf_y_mode_cdf[1][0][6], av1_kf_y_mode_cdf[1][0][7], av1_kf_y_mode_cdf[1][0][8],
                        av1_kf_y_mode_cdf[1][0][9], av1_kf_y_mode_cdf[1][0][10], av1_kf_y_mode_cdf[1][0][11],
                        av1_kf_y_mode_cdf[1][0][12]});
    push_symbol(3, 7, {av1_angle_delta_cdf[2][0], av1_angle_delta_cdf[2][1], av1_angle_delta_cdf[2][2],
                       av1_angle_delta_cdf[2][3], av1_angle_delta_cdf[2][4], av1_angle_delta_cdf[2][5],
                       av1_angle_delta_cdf[2][6]});
    push_symbol(0, 14, {av1_uv_mode_cdf_cfl[3][0], av1_uv_mode_cdf_cfl[3][1], av1_uv_mode_cdf_cfl[3][2],
                        av1_uv_mode_cdf_cfl[3][3], av1_uv_mode_cdf_cfl[3][4], av1_uv_mode_cdf_cfl[3][5],
                        av1_uv_mode_cdf_cfl[3][6], av1_uv_mode_cdf_cfl[3][7], av1_uv_mode_cdf_cfl[3][8],
                        av1_uv_mode_cdf_cfl[3][9], av1_uv_mode_cdf_cfl[3][10], av1_uv_mode_cdf_cfl[3][11],
                        av1_uv_mode_cdf_cfl[3][12], av1_uv_mode_cdf_cfl[3][13]});

    // Block (1,1): DC_PRED, qcoeff = {-1, 2, 1@8, -1@10}
    push_symbol(0, 4, {av1_partition_cdf[0][0], av1_partition_cdf[0][1], av1_partition_cdf[0][2], av1_partition_cdf[0][3]});
    push_symbol(0, 2, {av1_skip_cdf[1][0], av1_skip_cdf[1][1]});
    push_symbol(0, 13, {av1_kf_y_mode_cdf[4][3][0], av1_kf_y_mode_cdf[4][3][1], av1_kf_y_mode_cdf[4][3][2],
                        av1_kf_y_mode_cdf[4][3][3], av1_kf_y_mode_cdf[4][3][4], av1_kf_y_mode_cdf[4][3][5],
                        av1_kf_y_mode_cdf[4][3][6], av1_kf_y_mode_cdf[4][3][7], av1_kf_y_mode_cdf[4][3][8],
                        av1_kf_y_mode_cdf[4][3][9], av1_kf_y_mode_cdf[4][3][10], av1_kf_y_mode_cdf[4][3][11],
                        av1_kf_y_mode_cdf[4][3][12]});
    push_symbol(0, 14, {av1_uv_mode_cdf_cfl[0][0], av1_uv_mode_cdf_cfl[0][1], av1_uv_mode_cdf_cfl[0][2],
                        av1_uv_mode_cdf_cfl[0][3], av1_uv_mode_cdf_cfl[0][4], av1_uv_mode_cdf_cfl[0][5],
                        av1_uv_mode_cdf_cfl[0][6], av1_uv_mode_cdf_cfl[0][7], av1_uv_mode_cdf_cfl[0][8],
                        av1_uv_mode_cdf_cfl[0][9], av1_uv_mode_cdf_cfl[0][10], av1_uv_mode_cdf_cfl[0][11],
                        av1_uv_mode_cdf_cfl[0][12], av1_uv_mode_cdf_cfl[0][13]});
    push_symbol(0, 2, {av1_txb_skip_cdf[0][0], av1_txb_skip_cdf[0][1]});
    push_symbol(1, 7, {av1_intra_tx_type_cdf_8x8[0][0], av1_intra_tx_type_cdf_8x8[0][1], av1_intra_tx_type_cdf_8x8[0][2],
                       av1_intra_tx_type_cdf_8x8[0][3], av1_intra_tx_type_cdf_8x8[0][4], av1_intra_tx_type_cdf_8x8[0][5],
                       av1_intra_tx_type_cdf_8x8[0][6]});
    push_symbol(4, 7, {av1_eob_multi64_cdf[0][0], av1_eob_multi64_cdf[0][1], av1_eob_multi64_cdf[0][2],
                       av1_eob_multi64_cdf[0][3], av1_eob_multi64_cdf[0][4], av1_eob_multi64_cdf[0][5],
                       av1_eob_multi64_cdf[0][6]});
    push_symbol(0, 2, {av1_eob_extra_cdf[0][2][0], av1_eob_extra_cdf[0][2][1]});
    push_bool(0);
    push_bool(0);
    push_symbol(0, 3, {av1_coeff_base_eob_cdf[1][0], av1_coeff_base_eob_cdf[1][1], av1_coeff_base_eob_cdf[1][2]});
    push_symbol(0, 4, {av1_coeff_base_cdf[6][0], av1_coeff_base_cdf[6][1], av1_coeff_base_cdf[6][2], av1_coeff_base_cdf[6][3]});
    push_symbol(0, 4, {av1_coeff_base_cdf[6][0], av1_coeff_base_cdf[6][1], av1_coeff_base_cdf[6][2], av1_coeff_base_cdf[6][3]});
    push_symbol(0, 4, {av1_coeff_base_cdf[6][0], av1_coeff_base_cdf[6][1], av1_coeff_base_cdf[6][2], av1_coeff_base_cdf[6][3]});
    push_symbol(0, 4, {av1_coeff_base_cdf[7][0], av1_coeff_base_cdf[7][1], av1_coeff_base_cdf[7][2], av1_coeff_base_cdf[7][3]});
    push_symbol(0, 4, {av1_coeff_base_cdf[7][0], av1_coeff_base_cdf[7][1], av1_coeff_base_cdf[7][2], av1_coeff_base_cdf[7][3]});
    push_symbol(2, 4, {av1_coeff_base_cdf[2][0], av1_coeff_base_cdf[2][1], av1_coeff_base_cdf[2][2], av1_coeff_base_cdf[2][3]});
    push_symbol(1, 4, {av1_coeff_base_cdf[2][0], av1_coeff_base_cdf[2][1], av1_coeff_base_cdf[2][2], av1_coeff_base_cdf[2][3]});
    push_symbol(1, 4, {av1_coeff_base_cdf[0][0], av1_coeff_base_cdf[0][1], av1_coeff_base_cdf[0][2], av1_coeff_base_cdf[0][3]});
    push_symbol(1, 2, {av1_dc_sign_cdf[0][2][0], av1_dc_sign_cdf[0][2][1]});
    push_bool(0);
    push_bool(0);
    push_bool(1);
    push_symbol(1, 2, {av1_txb_skip_cdf_4x4[7][0], av1_txb_skip_cdf_4x4[7][1]});
    push_symbol(1, 2, {av1_txb_skip_cdf_4x4[7][0], av1_txb_skip_cdf_4x4[7][1]});

    return ops;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    run_case("empty", std::vector<Op>{});
    run_case("single-bool", {
        {Op::kBool, 1, 16384, 0},
    });
    run_case("mixed-small", {
        {Op::kBool, 0, 16384, 0},
        {Op::kBool, 1, 24576, 0},
        {Op::kBool, 1, 4096, 0},
        {Op::kLit,  0xA5, 0, 8},
        {Op::kLit,  0x03, 0, 3},
        {Op::kBool, 0, 30000, 0},
        {Op::kBool, 1, 12000, 0},
    });
    run_case("mixed-longer", {
        {Op::kBool, 1, 16384, 0, 0, {}},
        {Op::kBool, 0, 8192, 0, 0, {}},
        {Op::kBool, 1, 25000, 0, 0, {}},
        {Op::kLit,  0x5A, 0, 8, 0, {}},
        {Op::kLit,  0x1C, 0, 5, 0, {}},
        {Op::kBool, 1, 4096, 0, 0, {}},
        {Op::kBool, 0, 28672, 0, 0, {}},
        {Op::kLit,  0xF0, 0, 8, 0, {}},
        {Op::kBool, 1, 20000, 0, 0, {}},
        {Op::kBool, 0, 15000, 0, 0, {}},
        {Op::kBool, 1, 5000, 0, 0, {}},
    });
    run_case("symbol-mix", {
        {Op::kSymbol, 0, 0, 0, 2, {av1_skip_cdf[0][0], av1_skip_cdf[0][1]}},
        {Op::kSymbol, 1, 0, 0, 2, {av1_skip_cdf[1][0], av1_skip_cdf[1][1]}},
        {Op::kSymbol, 3, 0, 0, 4, {av1_partition_cdf[0][0], av1_partition_cdf[0][1], av1_partition_cdf[0][2], av1_partition_cdf[0][3]}},
        {Op::kSymbol, 7, 0, 0, 13, {av1_kf_y_mode_cdf[0][0][0], av1_kf_y_mode_cdf[0][0][1], av1_kf_y_mode_cdf[0][0][2], av1_kf_y_mode_cdf[0][0][3],
                                     av1_kf_y_mode_cdf[0][0][4], av1_kf_y_mode_cdf[0][0][5], av1_kf_y_mode_cdf[0][0][6], av1_kf_y_mode_cdf[0][0][7],
                                     av1_kf_y_mode_cdf[0][0][8], av1_kf_y_mode_cdf[0][0][9], av1_kf_y_mode_cdf[0][0][10], av1_kf_y_mode_cdf[0][0][11],
                                     av1_kf_y_mode_cdf[0][0][12]}},
        {Op::kBool, 1, 16384, 0, 0, {}},
    });
    run_case("probe-16x16-static", build_probe_16x16_static_case());

    std::fprintf(stderr, "All av1_entropy reference checks passed.\n");
    return 0;
}
