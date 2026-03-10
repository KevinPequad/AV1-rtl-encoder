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

    std::fprintf(stderr, "All av1_entropy reference checks passed.\n");
    return 0;
}
