#include <verilated.h>
#include "Vav1_encoder_top.h"
#include "Vav1_encoder_top___024root.h"  // Access to internal signals
#include "av1_bitstream_writer.h"
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

// Resolution set at compile time via -DFRAME_W=... -DFRAME_H=...
#ifndef FRAME_W
#define FRAME_W 1280
#endif
#ifndef FRAME_H
#define FRAME_H 720
#endif
static constexpr int FRAME_WIDTH  = FRAME_W;
static constexpr int FRAME_HEIGHT = FRAME_H;
static constexpr int FRAME_SIZE   = FRAME_WIDTH * FRAME_HEIGHT * 3 / 2;
static constexpr int LUMA_SIZE    = FRAME_WIDTH * FRAME_HEIGHT;
static constexpr int CHROMA_SIZE  = (FRAME_WIDTH / 2) * (FRAME_HEIGHT / 2);
static constexpr int BLK_COLS     = FRAME_WIDTH / 8;
static constexpr int BLK_ROWS     = FRAME_HEIGHT / 8;
static constexpr size_t DEFAULT_MAX_BITSTREAM = 64 * 1024 * 1024;

static std::vector<uint8_t> raw_pixel_mem;
static std::vector<uint8_t> bitstream_mem;
static std::vector<uint8_t> ref_frame_rd;
static std::vector<uint8_t> ref_frame_wr;
static std::vector<uint8_t> ref_cb_rd, ref_cb_wr;
static std::vector<uint8_t> ref_cr_rd, ref_cr_wr;
static volatile bool got_sigint = false;
static void sigint_handler(int) { got_sigint = true; }
static int16_t sign_extend_9(uint16_t v) {
    v &= 0x1FFu;
    return (v & 0x100u) ? static_cast<int16_t>(v | 0xFE00u) : static_cast<int16_t>(v);
}

struct EncodedTemporalUnit {
    uint64_t pts;
    bool is_keyframe;
    std::vector<uint8_t> payload;
};

struct PendingEntropyOp {
    enum Kind {
        Symbol,
        Bool,
    } kind;
    int value;
    int prob;
    int nsyms;
    std::vector<uint16_t> icdf;
};

// State for capturing RTL block data
static std::vector<AV1BitstreamWriter::BlockInfo> frame_blocks;
static int last_captured_blk = -1;

int main(int argc, char** argv) {
    std::signal(SIGINT, sigint_handler);
    Verilated::commandArgs(argc, argv);

    int num_frames = 1;
    int qindex = 128;
    int dc_only = 1;
    int all_key = 1;
    int dump_blocks = 0;
    int force_intra = 0;
    int zero_inter_coeffs = 0;
    int limit_newmv_blocks = -1;
    int limit_inter_blocks = -1;
    int override_first_newmvx = 0;
    int override_first_newmvy = 0;
    int override_first_newmv = 0;
    int only_full_coeff_block = -1;
    int max_coeff_block = -1;
    int force_first_ac_positive = 0;
    int force_first_ac_to_scan1 = 0;
    int coeff_debug = 0;
    int max_scan_coeffs = -1;
    int trace_block = -1;
    int dump_inter_summary = 0;
    int debug_zero_coeff_block = -1;
    int debug_zero_coeff_idx = -1;
    int debug_transpose_coeff_block = -1;
    int debug_add_coeff_block = -1;
    int debug_add_coeff_idx = -1;
    int debug_add_coeff_delta = 0;
    int static_cdf_mode = 0;
    int trace_entropy = 0;
    int trace_bs = 0;
    int trace_entropy_shadow = 0;
    int trace_writer_entropy = 0;
    uint64_t progress_every = 0;
    std::string input_file = "data/raw_frames.yuv";
    std::string output_file = "output/encoded.obu";
    uint64_t timeout_cycles = 500000000;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("+frames=", 0) == 0) num_frames = std::atoi(arg.c_str() + 8);
        else if (arg.rfind("+input=", 0) == 0) input_file = arg.substr(7);
        else if (arg.rfind("+output=", 0) == 0) output_file = arg.substr(8);
        else if (arg.rfind("+timeout=", 0) == 0) timeout_cycles = std::strtoull(arg.c_str() + 9, nullptr, 10);
        else if (arg.rfind("+qindex=", 0) == 0) qindex = std::atoi(arg.c_str() + 8);
        else if (arg.rfind("+dc_only=", 0) == 0) dc_only = std::atoi(arg.c_str() + 9);
        else if (arg.rfind("+all_key=", 0) == 0) all_key = std::atoi(arg.c_str() + 9);
        else if (arg.rfind("+dump_blocks=", 0) == 0) dump_blocks = std::atoi(arg.c_str() + 13);
        else if (arg.rfind("+force_intra=", 0) == 0) force_intra = std::atoi(arg.c_str() + 13);
        else if (arg.rfind("+zero_inter_coeffs=", 0) == 0) zero_inter_coeffs = std::atoi(arg.c_str() + 19);
        else if (arg.rfind("+limit_newmv_blocks=", 0) == 0) limit_newmv_blocks = std::atoi(arg.c_str() + 20);
        else if (arg.rfind("+limit_inter_blocks=", 0) == 0) limit_inter_blocks = std::atoi(arg.c_str() + 20);
        else if (arg.rfind("+override_first_newmvx=", 0) == 0) {
            override_first_newmvx = std::atoi(arg.c_str() + 23);
            override_first_newmv = 1;
        } else if (arg.rfind("+override_first_newmvy=", 0) == 0) {
            override_first_newmvy = std::atoi(arg.c_str() + 23);
            override_first_newmv = 1;
        } else if (arg.rfind("+only_full_coeff_block=", 0) == 0) {
            only_full_coeff_block = std::atoi(arg.c_str() + 23);
        } else if (arg.rfind("+max_coeff_block=", 0) == 0) {
            max_coeff_block = std::atoi(arg.c_str() + 17);
        } else if (arg.rfind("+force_first_ac_positive=", 0) == 0) {
            force_first_ac_positive = std::atoi(arg.c_str() + 25);
        } else if (arg.rfind("+force_first_ac_to_scan1=", 0) == 0) {
            force_first_ac_to_scan1 = std::atoi(arg.c_str() + 25);
        } else if (arg.rfind("+coeff_debug=", 0) == 0) {
            coeff_debug = std::atoi(arg.c_str() + 13);
        } else if (arg.rfind("+max_scan_coeffs=", 0) == 0) {
            max_scan_coeffs = std::atoi(arg.c_str() + 17);
        } else if (arg.rfind("+trace_block=", 0) == 0) {
            trace_block = std::atoi(arg.c_str() + 13);
        } else if (arg.rfind("+dump_inter_summary=", 0) == 0) {
            dump_inter_summary = std::atoi(arg.c_str() + 20);
        } else if (arg.rfind("+debug_zero_coeff_block=", 0) == 0) {
            debug_zero_coeff_block = std::atoi(arg.c_str() + 24);
        } else if (arg.rfind("+debug_zero_coeff_idx=", 0) == 0) {
            debug_zero_coeff_idx = std::atoi(arg.c_str() + 22);
        } else if (arg.rfind("+debug_transpose_coeff_block=", 0) == 0) {
            debug_transpose_coeff_block = std::atoi(arg.c_str() + 29);
        } else if (arg.rfind("+debug_add_coeff_block=", 0) == 0) {
            debug_add_coeff_block = std::atoi(arg.c_str() + 23);
        } else if (arg.rfind("+debug_add_coeff_idx=", 0) == 0) {
            debug_add_coeff_idx = std::atoi(arg.c_str() + 21);
        } else if (arg.rfind("+debug_add_coeff_delta=", 0) == 0) {
            debug_add_coeff_delta = std::atoi(arg.c_str() + 23);
        } else if (arg.rfind("+static_cdf_mode=", 0) == 0) {
            static_cdf_mode = std::atoi(arg.c_str() + 17);
        } else if (arg.rfind("+trace_entropy=", 0) == 0) {
            trace_entropy = std::atoi(arg.c_str() + 15);
        } else if (arg.rfind("+trace_bs=", 0) == 0) {
            trace_bs = std::atoi(arg.c_str() + 10);
        } else if (arg.rfind("+trace_entropy_shadow=", 0) == 0) {
            trace_entropy_shadow = std::atoi(arg.c_str() + 22);
        } else if (arg.rfind("+trace_writer_entropy=", 0) == 0) {
            trace_writer_entropy = std::atoi(arg.c_str() + 22);
        } else if (arg.rfind("+progress_every=", 0) == 0) {
            progress_every = std::strtoull(arg.c_str() + 16, nullptr, 10);
        }
    }

    const int effective_qindex = qindex <= 0 ? 1 : qindex;

    namespace fs = std::filesystem;
    const fs::path output_path(output_file);
    const fs::path output_dir = output_path.has_parent_path() ? output_path.parent_path() : fs::current_path();
    const fs::path still_dir = output_dir / "still_frames";
    const fs::path rtl_dir = output_dir / "rtl_frames";
    std::error_code fs_ec;
    fs::create_directories(output_dir, fs_ec);
    fs::remove_all(still_dir, fs_ec);
    fs::create_directories(still_dir, fs_ec);
    fs::remove_all(rtl_dir, fs_ec);
    fs::create_directories(rtl_dir, fs_ec);

    std::ifstream f(input_file, std::ios::binary);
    if (!f.is_open()) { fprintf(stderr, "[TB] ERROR: Cannot open %s\n", input_file.c_str()); return 1; }
    f.seekg(0, std::ios::end);
    size_t file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    int avail_frames = file_size / FRAME_SIZE;
    if (num_frames > avail_frames) num_frames = avail_frames;

    raw_pixel_mem.resize(num_frames * FRAME_SIZE);
    f.read(reinterpret_cast<char*>(raw_pixel_mem.data()), num_frames * FRAME_SIZE);
    f.close();

    bitstream_mem.assign(DEFAULT_MAX_BITSTREAM, 0);
    ref_frame_rd.assign(LUMA_SIZE, 128);
    ref_frame_wr.assign(LUMA_SIZE, 128);
    ref_cb_rd.assign(CHROMA_SIZE, 128);
    ref_cb_wr.assign(CHROMA_SIZE, 128);
    ref_cr_rd.assign(CHROMA_SIZE, 128);
    ref_cr_wr.assign(CHROMA_SIZE, 128);

    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "  AV1 RTL Encoder Testbench\n");
    fprintf(stderr, "  Frames: %d  Resolution: %dx%d  QIndex: %d  CoeffMode: %s  GOP: %s\n",
            num_frames, FRAME_WIDTH, FRAME_HEIGHT, effective_qindex,
            dc_only ? "DC-only" : "Full", all_key ? "all-key" : "IP");
    fprintf(stderr, "==========================================================\n");
    if (effective_qindex != qindex) {
        fprintf(stderr,
                "[TB] Requested qindex=%d clamps to qindex=%d; lossless TX_4X4 remains deferred.\n",
                qindex, effective_qindex);
    }
    if (override_first_newmv) {
        fprintf(stderr, "[TB] Writer override first NEWMV -> (%d,%d)\n",
                override_first_newmvx, override_first_newmvy);
    }

    Vav1_encoder_top* dut = new Vav1_encoder_top;
    dut->clk = 0; dut->rst_n = 0; dut->start = 0;
    dut->frame_num_in = 0; dut->is_keyframe_in = 0;
    dut->force_intra_in = force_intra ? 1 : 0;
    dut->dc_only_in = dc_only ? 1 : 0;
    dut->qindex_in = effective_qindex;
    dut->ref_mem_rd_data = 128;
    dut->chr_cb_ref_rd_data = 128;
    dut->chr_cr_ref_rd_data = 128;

    uint64_t cycle = 0;
    int frame_idx = 0;
    uint32_t total_bs_bytes = 0;
    bool frame_active = false;
    bool current_frame_is_key = true;
    std::vector<EncodedTemporalUnit> temporal_units;
    std::vector<EncodedTemporalUnit> rtl_temporal_units;
    std::vector<uint8_t> rtl_byte_stream;
    std::vector<PendingEntropyOp> entropy_req_log;
    std::vector<PendingEntropyOp> entropy_accept_log;
    std::vector<uint8_t> entropy_byte_log;
    AV1RangeCoder entropy_live_shadow;
    bool entropy_live_shadow_valid = false;
    bool entropy_state_mismatch = false;
    uint64_t next_progress_cycle = 0;

    // FSM state constants (must match av1_encoder_top.v)
    constexpr int TS_PREDICT = 11;
    constexpr int TS_WAIT_PRED = 12;
    constexpr int TS_REF_WR = 20;
    constexpr int TS_CHR_FETCH = 21;
    constexpr int TS_IXFORM_COL = 28;

    // Reset
    for (int i = 0; i < 20; i++) {
        dut->clk = 1; dut->eval(); dut->clk = 0; dut->eval();
        cycle++;
        if (cycle == 10) dut->rst_n = 1;
    }

    while (!got_sigint && cycle < timeout_cycles && frame_idx < num_frames) {
        if (!frame_active) {
            dut->start = 1;
            int idr_interval = 12;
            bool is_key = all_key ? true : (frame_idx % idr_interval == 0);
            dut->frame_num_in = all_key ? 0 : ((frame_idx % idr_interval) & 0xF);
            dut->is_keyframe_in = is_key ? 1 : 0;
            dut->force_intra_in = force_intra ? 1 : 0;
            dut->dc_only_in = dc_only ? 1 : 0;
            dut->qindex_in = effective_qindex;
            current_frame_is_key = is_key;
            frame_active = true;
            frame_blocks.clear();
            frame_blocks.resize(BLK_COLS * BLK_ROWS);
            last_captured_blk = -1;
            rtl_byte_stream.clear();
            entropy_req_log.clear();
            entropy_accept_log.clear();
            entropy_byte_log.clear();
            entropy_live_shadow.init();
            entropy_live_shadow_valid = true;
            entropy_state_mismatch = false;
            next_progress_cycle = progress_every ? (cycle + progress_every) : 0;
            fprintf(stderr, "[TB] Frame %d (%s) start @ cycle %llu\n",
                    frame_idx, is_key ? "KEY" : "INTER", (unsigned long long)cycle);
            std::fflush(stderr);
        }

        dut->clk = 1;

        // Raw pixel memory read
        {
            size_t base = (size_t)frame_idx * FRAME_SIZE;
            uint32_t addr = dut->raw_mem_addr;
            if (base + addr < raw_pixel_mem.size())
                dut->raw_mem_data = raw_pixel_mem[base + addr];
            else
                dut->raw_mem_data = 0;
        }

        // Reference frame memory read
        {
            uint32_t addr = dut->ref_mem_rd_addr;
            if (dut->ref_rd_is_neigh) {
                dut->ref_mem_rd_data = (addr < ref_frame_wr.size()) ? ref_frame_wr[addr] : 128;
            } else {
                dut->ref_mem_rd_data = (addr < ref_frame_rd.size()) ? ref_frame_rd[addr] : 128;
            }
        }

        // Chroma reference reads
        {
            uint32_t addr = dut->chr_cb_ref_rd_addr;
            dut->chr_cb_ref_rd_data = (addr < ref_cb_rd.size()) ? ref_cb_rd[addr] : 128;
        }
        {
            uint32_t addr = dut->chr_cr_ref_rd_addr;
            dut->chr_cr_ref_rd_data = (addr < ref_cr_rd.size()) ? ref_cr_rd[addr] : 128;
        }

        dut->eval();
        if (dut->start) dut->start = 0;
        if (progress_every && frame_active && cycle >= next_progress_cycle) {
            auto* root = dut->rootp;
            fprintf(stderr,
                    "[TB] progress frame=%d/%d cycle=%llu state=%d blk=(%d,%d) key=%d force_intra=%d use_inter=%d me_mv=(%d,%d) done=%d\n",
                    frame_idx, num_frames,
                    (unsigned long long)cycle,
                    root->av1_encoder_top__DOT__top_state,
                    root->av1_encoder_top__DOT__blk_x,
                    root->av1_encoder_top__DOT__blk_y,
                    dut->is_keyframe_in ? 1 : 0,
                    dut->force_intra_in ? 1 : 0,
                    root->av1_encoder_top__DOT__use_inter ? 1 : 0,
                    sign_extend_9(root->av1_encoder_top__DOT__me_mvx),
                    sign_extend_9(root->av1_encoder_top__DOT__me_mvy),
                    dut->done ? 1 : 0);
            std::fflush(stderr);
            do {
                next_progress_cycle += progress_every;
            } while (next_progress_cycle <= cycle);
        }

        // Capture block metadata once the luma writeback phase is complete.
        // Capturing on entry to TS_REF_WR was too early for some AC terms,
        // which caused the software writer to serialize stale coefficients.
        {
            auto* root = dut->rootp;
            int state = root->av1_encoder_top__DOT__top_state;
            int bx = root->av1_encoder_top__DOT__blk_x;
            int by = root->av1_encoder_top__DOT__blk_y;
            int blk_idx = by * BLK_COLS + bx;

            if (trace_entropy || trace_entropy_shadow) {
                if (root->av1_encoder_top__DOT__ec_encode_symbol) {
                    const unsigned nsyms = root->av1_encoder_top__DOT__ec_nsyms;
                    auto icdf_entry = [&](unsigned idx) -> unsigned {
                        const unsigned word = idx >> 1;
                        const unsigned shift = (idx & 1U) ? 16U : 0U;
                        return (root->av1_encoder_top__DOT__ec_icdf_flat[word] >> shift) & 0xFFFFU;
                    };
                    if (trace_entropy) {
                        fprintf(stderr,
                                "[ETRACE] blk=(%d,%d) state=%d sym=%u nsyms=%u icdf=",
                                bx, by, state,
                                root->av1_encoder_top__DOT__ec_symbol,
                                nsyms);
                        for (unsigned i = 0; i < nsyms; ++i) {
                            fprintf(stderr, "%s%u", i ? "," : "", icdf_entry(i));
                        }
                        fprintf(stderr, "\n");
                    }
                    if (trace_entropy_shadow) {
                        PendingEntropyOp op{};
                        op.kind = PendingEntropyOp::Symbol;
                        op.value = root->av1_encoder_top__DOT__ec_symbol;
                        op.prob = 0;
                        op.nsyms = static_cast<int>(nsyms);
                        op.icdf.reserve(nsyms);
                        for (unsigned i = 0; i < nsyms; ++i)
                            op.icdf.push_back(static_cast<uint16_t>(icdf_entry(i)));
                        entropy_req_log.push_back(std::move(op));
                    }
                }
                if (root->av1_encoder_top__DOT__ec_encode_bool) {
                    if (trace_entropy) {
                        fprintf(stderr,
                                "[ETRACE] blk=(%d,%d) state=%d bool=%u prob=%u\n",
                                bx, by, state,
                                root->av1_encoder_top__DOT__ec_bool_val,
                                root->av1_encoder_top__DOT__ec_bool_prob);
                    }
                    if (trace_entropy_shadow) {
                        PendingEntropyOp op{};
                        op.kind = PendingEntropyOp::Bool;
                        op.value = root->av1_encoder_top__DOT__ec_bool_val ? 1 : 0;
                        op.prob = root->av1_encoder_top__DOT__ec_bool_prob;
                        op.nsyms = 0;
                        entropy_req_log.push_back(std::move(op));
                    }
                }
                if (dut->ec_dbg_accept_valid_out) {
                    if (dut->ec_dbg_accept_kind_out == 2) {
                        const unsigned nsyms = dut->ec_dbg_accept_nsyms_out;
                        auto icdf_entry = [&](unsigned idx) -> unsigned {
                            const unsigned word = idx >> 1;
                            const unsigned shift = (idx & 1U) ? 16U : 0U;
                            return (dut->ec_dbg_accept_icdf_flat_out[word] >> shift) & 0xFFFFU;
                        };
                        if (trace_entropy) {
                            fprintf(stderr,
                                "[EACC] blk=(%d,%d) state=%d sym=%u nsyms=%u icdf=",
                                bx, by, state,
                                dut->ec_dbg_accept_symbol_out,
                                nsyms);
                            for (unsigned i = 0; i < nsyms; ++i) {
                                fprintf(stderr, "%s%u", i ? "," : "", icdf_entry(i));
                            }
                            fprintf(stderr, "\n");
                        }
                        if (trace_entropy_shadow) {
                            PendingEntropyOp op{};
                            op.kind = PendingEntropyOp::Symbol;
                            op.value = dut->ec_dbg_accept_symbol_out;
                            op.prob = 0;
                            op.nsyms = static_cast<int>(nsyms);
                            op.icdf.reserve(nsyms);
                            for (unsigned i = 0; i < nsyms; ++i)
                                op.icdf.push_back(static_cast<uint16_t>(icdf_entry(i)));
                            entropy_accept_log.push_back(std::move(op));
                            if (entropy_live_shadow_valid) {
                                const auto& applied = entropy_accept_log.back();
                                entropy_live_shadow.encode_symbol(applied.value, applied.icdf.data(), applied.nsyms);
                                const auto dut_rng = static_cast<unsigned>(root->av1_encoder_top__DOT__u_entropy__DOT__rng_reg);
                                const auto dut_low = static_cast<uint64_t>(root->av1_encoder_top__DOT__u_entropy__DOT__low_reg);
                                const auto dut_cnt = static_cast<int32_t>(root->av1_encoder_top__DOT__u_entropy__DOT__cnt_reg);
                                const auto dut_buf = static_cast<size_t>(root->av1_encoder_top__DOT__u_entropy__DOT__out_len);
                                if (!entropy_state_mismatch &&
                                    (entropy_live_shadow.rng_state() != dut_rng ||
                                     entropy_live_shadow.low_state() != dut_low ||
                                     entropy_live_shadow.cnt_state() != dut_cnt ||
                                     entropy_live_shadow.buf_size() != dut_buf)) {
                                    entropy_state_mismatch = true;
                                    fprintf(stderr,
                                            "[ESTATE] kind=symbol idx=%zu blk=(%d,%d) state=%d rng shadow=%u dut=%u low shadow=%llu dut=%llu cnt shadow=%d dut=%d buf shadow=%zu dut=%zu\n",
                                            entropy_accept_log.size() - 1, bx, by, state,
                                            entropy_live_shadow.rng_state(), dut_rng,
                                            (unsigned long long)entropy_live_shadow.low_state(),
                                            (unsigned long long)dut_low,
                                            entropy_live_shadow.cnt_state(), dut_cnt,
                                            entropy_live_shadow.buf_size(), dut_buf);
                                }
                            }
                        }
                    } else if (dut->ec_dbg_accept_kind_out == 1) {
                        if (trace_entropy) {
                            fprintf(stderr,
                                    "[EACC] blk=(%d,%d) state=%d bool=%u prob=%u\n",
                                    bx, by, state,
                                    dut->ec_dbg_accept_bool_val_out,
                                    dut->ec_dbg_accept_bool_prob_out);
                        }
                        if (trace_entropy_shadow) {
                            PendingEntropyOp op{};
                            op.kind = PendingEntropyOp::Bool;
                            op.value = dut->ec_dbg_accept_bool_val_out ? 1 : 0;
                            op.prob = dut->ec_dbg_accept_bool_prob_out;
                            op.nsyms = 0;
                            entropy_accept_log.push_back(std::move(op));
                            if (entropy_live_shadow_valid) {
                                const auto& applied = entropy_accept_log.back();
                                entropy_live_shadow.encode_bool(applied.value, applied.prob);
                                const auto dut_rng = static_cast<unsigned>(root->av1_encoder_top__DOT__u_entropy__DOT__rng_reg);
                                const auto dut_low = static_cast<uint64_t>(root->av1_encoder_top__DOT__u_entropy__DOT__low_reg);
                                const auto dut_cnt = static_cast<int32_t>(root->av1_encoder_top__DOT__u_entropy__DOT__cnt_reg);
                                const auto dut_buf = static_cast<size_t>(root->av1_encoder_top__DOT__u_entropy__DOT__out_len);
                                if (!entropy_state_mismatch &&
                                    (entropy_live_shadow.rng_state() != dut_rng ||
                                     entropy_live_shadow.low_state() != dut_low ||
                                     entropy_live_shadow.cnt_state() != dut_cnt ||
                                     entropy_live_shadow.buf_size() != dut_buf)) {
                                    entropy_state_mismatch = true;
                                    fprintf(stderr,
                                            "[ESTATE] kind=bool idx=%zu blk=(%d,%d) state=%d rng shadow=%u dut=%u low shadow=%llu dut=%llu cnt shadow=%d dut=%d buf shadow=%zu dut=%zu\n",
                                            entropy_accept_log.size() - 1, bx, by, state,
                                            entropy_live_shadow.rng_state(), dut_rng,
                                            (unsigned long long)entropy_live_shadow.low_state(),
                                            (unsigned long long)dut_low,
                                            entropy_live_shadow.cnt_state(), dut_cnt,
                                            entropy_live_shadow.buf_size(), dut_buf);
                                }
                            }
                        }
                    }
                }
            }
            if (trace_bs && dut->bs_mem_wr) {
                fprintf(stderr, "[WBYTE] addr=%u data=%02x src(h=%d e=%d m=%d)\n",
                        dut->bs_mem_addr, dut->bs_mem_data,
                        root->av1_encoder_top__DOT__bs_byte_valid ? 1 : 0,
                        root->av1_encoder_top__DOT__ec_byte_valid ? 1 : 0,
                        root->av1_encoder_top__DOT__manual_bs_wr ? 1 : 0);
            }

            if (state == TS_CHR_FETCH && blk_idx != last_captured_blk) {
                last_captured_blk = blk_idx;
                if (blk_idx < (int)frame_blocks.size()) {
                    auto& bi = frame_blocks[blk_idx];
                    for (int i = 0; i < 64; i++) {
                        bi.qcoeff[i] = (int16_t)root->av1_encoder_top__DOT__qcoeff[i];
                    }
                    bi.pred_mode = root->av1_encoder_top__DOT__best_intra_mode;
                    bi.is_inter = root->av1_encoder_top__DOT__use_inter;
                    bi.mvx = sign_extend_9(root->av1_encoder_top__DOT__me_mvx);
                    bi.mvy = sign_extend_9(root->av1_encoder_top__DOT__me_mvy);
                }
            }

            if (trace_block >= 0 && blk_idx == trace_block &&
                (state == TS_PREDICT || state == TS_WAIT_PRED || state == TS_IXFORM_COL || state == TS_REF_WR)) {
                fprintf(stderr,
                        "[TRACE] blk=%d state=%d mode=%u use_inter=%d top_left=%u has_top=%d has_left=%d\n",
                        blk_idx, state, root->av1_encoder_top__DOT__best_intra_mode,
                        root->av1_encoder_top__DOT__use_inter ? 1 : 0,
                        root->av1_encoder_top__DOT__top_left_pixel,
                        root->av1_encoder_top__DOT__has_top ? 1 : 0,
                        root->av1_encoder_top__DOT__has_left ? 1 : 0);
                fprintf(stderr, "[TRACE] top=");
                for (int i = 0; i < 8; ++i)
                    fprintf(stderr, "%s%u", i ? "," : "", root->av1_encoder_top__DOT__top_pixels[i]);
                fprintf(stderr, "\n");
                fprintf(stderr, "[TRACE] left=");
                for (int i = 0; i < 8; ++i)
                    fprintf(stderr, "%s%u", i ? "," : "", root->av1_encoder_top__DOT__left_pixels[i]);
                fprintf(stderr, "\n");
                fprintf(stderr, "[TRACE] pred=");
                for (int i = 0; i < 64; ++i)
                    fprintf(stderr, "%s%u", i ? "," : "", root->av1_encoder_top__DOT__pred_blk[i]);
                fprintf(stderr, "\n");
                if (state == TS_IXFORM_COL && root->av1_encoder_top__DOT__xform_col == 0) {
                    fprintf(stderr, "[TRACE] dqcoeff=");
                    for (int i = 0; i < 64; ++i)
                        fprintf(stderr, "%s%d", i ? "," : "", (int32_t)root->av1_encoder_top__DOT__residual[i]);
                    fprintf(stderr, "\n");
                }
                if (state == TS_REF_WR) {
                    fprintf(stderr, "[TRACE] qcoeff=");
                    for (int i = 0; i < 64; ++i)
                        fprintf(stderr, "%s%d", i ? "," : "", (int16_t)root->av1_encoder_top__DOT__qcoeff[i]);
                    fprintf(stderr, "\n");
                    fprintf(stderr, "[TRACE] residual=");
                    for (int i = 0; i < 64; ++i)
                        fprintf(stderr, "%s%d", i ? "," : "", (int16_t)root->av1_encoder_top__DOT__residual[i]);
                    fprintf(stderr, "\n");
                    fprintf(stderr, "[TRACE] recon=");
                    for (int i = 0; i < 64; ++i)
                        fprintf(stderr, "%s%u", i ? "," : "", root->av1_encoder_top__DOT__recon_blk[i]);
                    fprintf(stderr, "\n");
                }
            }
        }

        if (trace_entropy_shadow && dut->rootp->av1_encoder_top__DOT__ec_byte_valid) {
            entropy_byte_log.push_back(static_cast<uint8_t>(dut->rootp->av1_encoder_top__DOT__ec_byte_out));
        }

        // Bitstream memory write
        if (dut->bs_mem_wr) {
            uint32_t addr = dut->bs_mem_addr;
            if (addr < bitstream_mem.size())
                bitstream_mem[addr] = dut->bs_mem_data;
            if (rtl_byte_stream.size() <= addr)
                rtl_byte_stream.resize(static_cast<size_t>(addr) + 1, 0);
            rtl_byte_stream[addr] = dut->bs_mem_data;
        }

        // Reference frame write-back
        if (dut->ref_mem_wr_en) {
            uint32_t addr = dut->ref_mem_wr_addr;
            if (addr < ref_frame_wr.size())
                ref_frame_wr[addr] = dut->ref_mem_wr_data;
        }

        if (dut->chr_cb_ref_wr_en) {
            uint32_t addr = dut->chr_cb_ref_wr_addr;
            if (addr < ref_cb_wr.size())
                ref_cb_wr[addr] = dut->chr_cb_ref_wr_data;
        }
        if (dut->chr_cr_ref_wr_en) {
            uint32_t addr = dut->chr_cr_ref_wr_addr;
            if (addr < ref_cr_wr.size())
                ref_cr_wr[addr] = dut->chr_cr_ref_wr_data;
        }

        if (dut->done) {
            if (trace_entropy_shadow) {
                auto replay_ops = [](const std::vector<PendingEntropyOp>& ops) {
                    AV1RangeCoder rc;
                    rc.init();
                    for (const auto& op : ops) {
                        if (op.kind == PendingEntropyOp::Symbol)
                            rc.encode_symbol(op.value, op.icdf.data(), op.nsyms);
                        else
                            rc.encode_bool(op.value, op.prob);
                    }
                    return rc.finish();
                };
                auto req_tile = replay_ops(entropy_req_log);
                auto accept_tile = replay_ops(entropy_accept_log);
                if (!entropy_state_mismatch && entropy_live_shadow_valid) {
                    fprintf(stderr,
                            "[ESTATE] accepted-stream state matched through all ops: rng=%u low=%llu cnt=%d buf=%zu\n",
                            entropy_live_shadow.rng_state(),
                            (unsigned long long)entropy_live_shadow.low_state(),
                            entropy_live_shadow.cnt_state(),
                            entropy_live_shadow.buf_size());
                }
                fprintf(stderr,
                        "[ESHADOW] reqs=%zu req_bytes=%zu accepts=%zu acc_bytes=%zu ec_bytes=%zu req_hex=",
                        entropy_req_log.size(), req_tile.size(),
                        entropy_accept_log.size(), accept_tile.size(),
                        entropy_byte_log.size());
                for (uint8_t b : req_tile) fprintf(stderr, "%02x", b);
                fprintf(stderr, " acc_hex=");
                for (uint8_t b : accept_tile) fprintf(stderr, "%02x", b);
                fprintf(stderr, " ec_hex=");
                for (uint8_t b : entropy_byte_log) fprintf(stderr, "%02x", b);
                fprintf(stderr, "\n");
            }
            total_bs_bytes = dut->bs_bytes_written;
            fprintf(stderr, "[TB] Frame %d done @ cycle %llu -- rtl_bs_bytes=%u\n",
                    frame_idx, (unsigned long long)cycle, total_bs_bytes);
            {
                const size_t rtl_bytes = rtl_byte_stream.size();
                if (rtl_bytes != static_cast<size_t>(total_bs_bytes)) {
                    fprintf(stderr,
                            "[TB] RTL byte capture size mismatch: direct=%zu bs_bytes_written=%u\n",
                            rtl_bytes, total_bs_bytes);
                }
                std::vector<uint8_t> rtl_frame_payload(
                    rtl_byte_stream.begin(),
                    rtl_byte_stream.begin() +
                        std::min(rtl_byte_stream.size(), static_cast<size_t>(total_bs_bytes)));
                rtl_temporal_units.push_back({static_cast<uint64_t>(frame_idx), current_frame_is_key,
                                              std::move(rtl_frame_payload)});

                char rtl_frame_name[32];
                std::snprintf(rtl_frame_name, sizeof(rtl_frame_name), "frame_%04d_rtl_raw.obu", frame_idx);
                fs::path rtl_frame_path = rtl_dir / rtl_frame_name;
                std::ofstream rtl_frame_out(rtl_frame_path, std::ios::binary);
                if (rtl_frame_out.is_open()) {
                    const auto& payload = rtl_temporal_units.back().payload;
                    rtl_frame_out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
                    rtl_frame_out.close();
                    fprintf(stderr, "[TB] Wrote RTL raw bytes: %zu bytes to %s\n",
                            payload.size(), rtl_frame_path.string().c_str());
                }
            }

            // Build proper AV1 bitstream using captured coefficients
            {
                AV1BitstreamWriter writer(FRAME_WIDTH, FRAME_HEIGHT, effective_qindex);
                writer.set_dc_only_mode(dc_only != 0);
                writer.set_coeff_debug_mode(coeff_debug != 0);
                writer.set_disable_cdf_update_mode(static_cdf_mode != 0);
                writer.set_trace_symbol_ops(trace_writer_entropy != 0);
                if (!all_key) {
                    writer.set_still_picture_mode(false);
                    writer.set_include_sequence_header(true);
                    writer.set_force_video_intra_only((num_frames > 1) && (frame_idx == 0));
                    writer.set_keyframe(current_frame_is_key);
                }
                int kept_newmv_blocks = 0;
                int kept_inter_blocks = 0;
                bool first_newmv_overridden = false;
                bool first_ac_forced_positive = false;
                bool first_ac_moved_to_scan1 = false;
                int writer_block_idx = 0;
                for (auto bi : frame_blocks) {
                    if (limit_inter_blocks >= 0 && bi.is_inter) {
                        if (kept_inter_blocks >= limit_inter_blocks) {
                            bi.is_inter = false;
                            bi.mvx = 0;
                            bi.mvy = 0;
                        } else {
                            ++kept_inter_blocks;
                        }
                    }
                    if (limit_newmv_blocks >= 0 && bi.is_inter && (bi.mvx != 0 || bi.mvy != 0)) {
                        if (kept_newmv_blocks >= limit_newmv_blocks) {
                            bi.mvx = 0;
                            bi.mvy = 0;
                        } else {
                            ++kept_newmv_blocks;
                        }
                    }
                    if (override_first_newmv && bi.is_inter && !first_newmv_overridden &&
                        (bi.mvx != 0 || bi.mvy != 0)) {
                        bi.mvx = override_first_newmvx;
                        bi.mvy = override_first_newmvy;
                        fprintf(stderr, "[TB] Override standalone first NEWMV -> (%d,%d)\n",
                                bi.mvx, bi.mvy);
                        first_newmv_overridden = true;
                    }
                    if (zero_inter_coeffs && bi.is_inter) {
                        std::memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
                    }
                    if (only_full_coeff_block >= 0 && writer_block_idx != only_full_coeff_block) {
                        std::memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
                    }
                    if (max_coeff_block >= 0 && writer_block_idx > max_coeff_block) {
                        std::memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
                    }
                    if (max_scan_coeffs >= 0 && max_scan_coeffs < 64) {
                        for (int scan_idx = max_scan_coeffs; scan_idx < 64; ++scan_idx)
                            bi.qcoeff[default_scan_8x8[scan_idx]] = 0;
                    }
                    if (force_first_ac_positive && !first_ac_forced_positive) {
                        for (int scan_idx = 1; scan_idx < 64; ++scan_idx) {
                            const int coeff_idx = default_scan_8x8[scan_idx];
                            if (bi.qcoeff[coeff_idx] != 0) {
                                if (bi.qcoeff[coeff_idx] < 0)
                                    bi.qcoeff[coeff_idx] = static_cast<int16_t>(-bi.qcoeff[coeff_idx]);
                                first_ac_forced_positive = true;
                                break;
                            }
                        }
                    }
                    if (force_first_ac_to_scan1 && !first_ac_moved_to_scan1) {
                        for (int scan_idx = 1; scan_idx < 64; ++scan_idx) {
                            const int coeff_idx = default_scan_8x8[scan_idx];
                            if (bi.qcoeff[coeff_idx] != 0) {
                                if (coeff_idx != default_scan_8x8[1]) {
                                    bi.qcoeff[default_scan_8x8[1]] = bi.qcoeff[coeff_idx];
                                    bi.qcoeff[coeff_idx] = 0;
                                }
                                first_ac_moved_to_scan1 = true;
                                break;
                            }
                        }
                    }
                    if (writer_block_idx == debug_zero_coeff_block &&
                        debug_zero_coeff_idx >= 0 && debug_zero_coeff_idx < 64) {
                        bi.qcoeff[debug_zero_coeff_idx] = 0;
                    }
                    if (writer_block_idx == debug_transpose_coeff_block) {
                        int16_t transposed[64];
                        for (int ty = 0; ty < 8; ++ty) {
                            for (int tx = 0; tx < 8; ++tx) {
                                transposed[ty * 8 + tx] = bi.qcoeff[tx * 8 + ty];
                            }
                        }
                        std::memcpy(bi.qcoeff, transposed, sizeof(transposed));
                    }
                    if (writer_block_idx == debug_add_coeff_block &&
                        debug_add_coeff_idx >= 0 && debug_add_coeff_idx < 64 &&
                        debug_add_coeff_delta != 0) {
                        bi.qcoeff[debug_add_coeff_idx] =
                            static_cast<int16_t>(bi.qcoeff[debug_add_coeff_idx] + debug_add_coeff_delta);
                    }
                    writer.add_block(bi);
                    ++writer_block_idx;
                }
                auto ivf_data = writer.write_ivf_frame();

                char frame_name[32];
                std::snprintf(frame_name, sizeof(frame_name), "frame_%04d.ivf", frame_idx);
                fs::path ivf_path = still_dir / frame_name;
                std::ofstream ivf_out(ivf_path, std::ios::binary);
                if (ivf_out.is_open()) {
                    ivf_out.write(reinterpret_cast<char*>(ivf_data.data()), ivf_data.size());
                    ivf_out.close();
                    fprintf(stderr, "[TB] Wrote AV1/IVF: %zu bytes to %s\n",
                            ivf_data.size(), ivf_path.string().c_str());
                }
            }

            {
                AV1BitstreamWriter writer(FRAME_WIDTH, FRAME_HEIGHT, effective_qindex);
                writer.set_dc_only_mode(dc_only != 0);
                writer.set_coeff_debug_mode(coeff_debug != 0);
                writer.set_disable_cdf_update_mode(static_cdf_mode != 0);
                writer.set_trace_symbol_ops(trace_writer_entropy != 0);
                writer.set_still_picture_mode(false);
                writer.set_include_sequence_header(frame_idx == 0);
                writer.set_force_video_intra_only(!all_key && (num_frames > 1) && (frame_idx == 0));
                writer.set_keyframe(current_frame_is_key);
                int kept_newmv_blocks = 0;
                int kept_inter_blocks = 0;
                bool first_newmv_overridden = false;
                bool first_ac_forced_positive = false;
                bool first_ac_moved_to_scan1 = false;
                int writer_block_idx = 0;
                for (auto bi : frame_blocks) {
                    if (limit_inter_blocks >= 0 && bi.is_inter) {
                        if (kept_inter_blocks >= limit_inter_blocks) {
                            bi.is_inter = false;
                            bi.mvx = 0;
                            bi.mvy = 0;
                        } else {
                            ++kept_inter_blocks;
                        }
                    }
                    if (limit_newmv_blocks >= 0 && bi.is_inter && (bi.mvx != 0 || bi.mvy != 0)) {
                        if (kept_newmv_blocks >= limit_newmv_blocks) {
                            bi.mvx = 0;
                            bi.mvy = 0;
                        } else {
                            ++kept_newmv_blocks;
                        }
                    }
                    if (override_first_newmv && bi.is_inter && !first_newmv_overridden &&
                        (bi.mvx != 0 || bi.mvy != 0)) {
                        bi.mvx = override_first_newmvx;
                        bi.mvy = override_first_newmvy;
                        fprintf(stderr, "[TB] Override sequence first NEWMV -> (%d,%d)\n",
                                bi.mvx, bi.mvy);
                        first_newmv_overridden = true;
                    }
                    if (zero_inter_coeffs && bi.is_inter) {
                        std::memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
                    }
                    if (only_full_coeff_block >= 0 && writer_block_idx != only_full_coeff_block) {
                        std::memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
                    }
                    if (max_coeff_block >= 0 && writer_block_idx > max_coeff_block) {
                        std::memset(bi.qcoeff, 0, sizeof(bi.qcoeff));
                    }
                    if (max_scan_coeffs >= 0 && max_scan_coeffs < 64) {
                        for (int scan_idx = max_scan_coeffs; scan_idx < 64; ++scan_idx)
                            bi.qcoeff[default_scan_8x8[scan_idx]] = 0;
                    }
                    if (force_first_ac_positive && !first_ac_forced_positive) {
                        for (int scan_idx = 1; scan_idx < 64; ++scan_idx) {
                            const int coeff_idx = default_scan_8x8[scan_idx];
                            if (bi.qcoeff[coeff_idx] != 0) {
                                if (bi.qcoeff[coeff_idx] < 0)
                                    bi.qcoeff[coeff_idx] = static_cast<int16_t>(-bi.qcoeff[coeff_idx]);
                                first_ac_forced_positive = true;
                                break;
                            }
                        }
                    }
                    if (force_first_ac_to_scan1 && !first_ac_moved_to_scan1) {
                        for (int scan_idx = 1; scan_idx < 64; ++scan_idx) {
                            const int coeff_idx = default_scan_8x8[scan_idx];
                            if (bi.qcoeff[coeff_idx] != 0) {
                                if (coeff_idx != default_scan_8x8[1]) {
                                    bi.qcoeff[default_scan_8x8[1]] = bi.qcoeff[coeff_idx];
                                    bi.qcoeff[coeff_idx] = 0;
                                }
                                first_ac_moved_to_scan1 = true;
                                break;
                            }
                        }
                    }
                    if (writer_block_idx == debug_zero_coeff_block &&
                        debug_zero_coeff_idx >= 0 && debug_zero_coeff_idx < 64) {
                        bi.qcoeff[debug_zero_coeff_idx] = 0;
                    }
                    if (writer_block_idx == debug_transpose_coeff_block) {
                        int16_t transposed[64];
                        for (int ty = 0; ty < 8; ++ty) {
                            for (int tx = 0; tx < 8; ++tx) {
                                transposed[ty * 8 + tx] = bi.qcoeff[tx * 8 + ty];
                            }
                        }
                        std::memcpy(bi.qcoeff, transposed, sizeof(transposed));
                    }
                    if (writer_block_idx == debug_add_coeff_block &&
                        debug_add_coeff_idx >= 0 && debug_add_coeff_idx < 64 &&
                        debug_add_coeff_delta != 0) {
                        bi.qcoeff[debug_add_coeff_idx] =
                            static_cast<int16_t>(bi.qcoeff[debug_add_coeff_idx] + debug_add_coeff_delta);
                    }
                    writer.add_block(bi);
                    ++writer_block_idx;
                }
                auto temporal_unit = writer.write_temporal_unit();
                temporal_units.push_back({static_cast<uint64_t>(frame_idx), current_frame_is_key, std::move(temporal_unit)});
            }

            if (dump_blocks) {
                for (size_t bi_idx = 0; bi_idx < frame_blocks.size(); ++bi_idx) {
                    const auto& bi = frame_blocks[bi_idx];
                    int nonzero = 0;
                    for (int i = 0; i < 64; ++i) {
                        int16_t coeff = dc_only ? (i == 0 ? bi.qcoeff[0] : 0) : bi.qcoeff[i];
                        if (coeff != 0) nonzero++;
                    }
                    if (!nonzero && bi.pred_mode == 0 && !bi.is_inter) continue;

                    fprintf(stderr,
                            "[TB] blk=%zu mode=%u inter=%d mv=(%d,%d) dc=%d nz=%d qcoeff[0..7]=",
                            bi_idx, bi.pred_mode, bi.is_inter ? 1 : 0,
                            bi.mvx, bi.mvy, bi.qcoeff[0], nonzero);
                    for (int i = 0; i < 8; ++i) {
                        fprintf(stderr, "%s%d", (i == 0) ? "" : ",", bi.qcoeff[i]);
                    }
                    fprintf(stderr, "\n");
                }
            }

            if (dump_inter_summary) {
                int inter_count = 0;
                int nonzero_inter_count = 0;
                int first_inter_idx = -1;
                for (size_t bi_idx = 0; bi_idx < frame_blocks.size(); ++bi_idx) {
                    const auto& bi = frame_blocks[bi_idx];
                    if (!bi.is_inter) continue;
                    int nonzero = 0;
                    for (int i = 0; i < 64; ++i) {
                        if (bi.qcoeff[i] != 0) ++nonzero;
                    }
                    if (first_inter_idx < 0) first_inter_idx = static_cast<int>(bi_idx);
                    ++inter_count;
                    if (nonzero) ++nonzero_inter_count;
                    fprintf(stderr,
                            "[TB] inter_summary frame=%d blk=%zu mv=(%d,%d) mode=%u dc=%d nz=%d\n",
                            frame_idx, bi_idx, bi.mvx, bi.mvy, bi.pred_mode, bi.qcoeff[0], nonzero);
                }
                fprintf(stderr,
                        "[TB] inter_summary frame=%d total_inter=%d nonzero_inter=%d first_inter_blk=%d\n",
                        frame_idx, inter_count, nonzero_inter_count, first_inter_idx);
            }

            // Dump encoder reconstruction as YUV
            {
                static std::ofstream recon_yuv;
                if (frame_idx == 0) {
                    std::string recon_path = output_file;
                    auto pos = recon_path.rfind('/');
                    if (pos != std::string::npos)
                        recon_path = recon_path.substr(0, pos + 1) + "recon.yuv";
                    else
                        recon_path = "recon.yuv";
                    recon_yuv.open(recon_path, std::ios::binary);
                    fprintf(stderr, "[TB] Writing recon to %s\n", recon_path.c_str());
                }
                if (recon_yuv.is_open()) {
                    recon_yuv.write(reinterpret_cast<char*>(ref_frame_wr.data()), LUMA_SIZE);
                    recon_yuv.write(reinterpret_cast<char*>(ref_cb_wr.data()), CHROMA_SIZE);
                    recon_yuv.write(reinterpret_cast<char*>(ref_cr_wr.data()), CHROMA_SIZE);
                }
                ref_frame_rd = ref_frame_wr;
                ref_cb_rd = ref_cb_wr;
                ref_cr_rd = ref_cr_wr;
            }

            // Luma stats
            {
                uint64_t sum = 0; uint8_t mn = 255, mx = 0;
                for (int i = 0; i < LUMA_SIZE; i++) {
                    sum += ref_frame_wr[i];
                    if (ref_frame_wr[i] < mn) mn = ref_frame_wr[i];
                    if (ref_frame_wr[i] > mx) mx = ref_frame_wr[i];
                }
                fprintf(stderr, "[TB] Ref frame luma: avg=%llu min=%u max=%u\n",
                        (unsigned long long)(sum / LUMA_SIZE), mn, mx);
            }

            frame_idx++;
            frame_active = false;
        }

        dut->clk = 0; dut->eval(); cycle++;
    }

    if (frame_idx < num_frames) {
        auto* root = dut->rootp;
        fprintf(stderr,
                "[TB] EXIT before completion: frame_idx=%d/%d cycle=%llu timeout=%llu state=%d blk=(%d,%d) "
                "use_inter=%d me_mv=(%d,%d) inter_fetch_idx=%d done=%d\n",
                frame_idx, num_frames,
                (unsigned long long)cycle,
                (unsigned long long)timeout_cycles,
                root->av1_encoder_top__DOT__top_state,
                root->av1_encoder_top__DOT__blk_x,
                root->av1_encoder_top__DOT__blk_y,
                root->av1_encoder_top__DOT__use_inter ? 1 : 0,
                sign_extend_9(root->av1_encoder_top__DOT__me_mvx),
                sign_extend_9(root->av1_encoder_top__DOT__me_mvy),
                root->av1_encoder_top__DOT__inter_fetch_idx,
                dut->done ? 1 : 0);
    }

    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "[TB] %d frames encoded, %llu cycles, rtl_bs=%u bytes\n",
            frame_idx, (unsigned long long)cycle, total_bs_bytes);
    fprintf(stderr, "==========================================================\n");

    if (!temporal_units.empty()) {
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> sequence_packets;
        sequence_packets.reserve(temporal_units.size());
        std::vector<uint8_t> obu_stream;
        for (const auto& tu : temporal_units) {
            sequence_packets.push_back({tu.pts, tu.payload});
            obu_stream.insert(obu_stream.end(), tu.payload.begin(), tu.payload.end());
        }

        std::ofstream obu_out(output_file, std::ios::binary | std::ios::trunc);
        if (obu_out.is_open()) {
            obu_out.write(reinterpret_cast<const char*>(obu_stream.data()), obu_stream.size());
            obu_out.close();
        }

        fs::path seq_ivf_path = output_path;
        if (seq_ivf_path.extension() == ".obu")
            seq_ivf_path.replace_extension(".ivf");
        else
            seq_ivf_path += ".ivf";
        auto ivf_sequence = AV1BitstreamWriter::write_ivf_sequence(FRAME_WIDTH, FRAME_HEIGHT, sequence_packets);
        std::ofstream seq_ivf_out(seq_ivf_path, std::ios::binary);
        if (seq_ivf_out.is_open()) {
            seq_ivf_out.write(reinterpret_cast<const char*>(ivf_sequence.data()), ivf_sequence.size());
            seq_ivf_out.close();
            fprintf(stderr, "[TB] Wrote AV1 sequence IVF: %zu bytes to %s\n",
                    ivf_sequence.size(), seq_ivf_path.string().c_str());
        }
    }

    if (!rtl_temporal_units.empty()) {
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> rtl_sequence_packets;
        std::vector<uint8_t> rtl_stream;
        size_t rtl_total = 0;
        rtl_sequence_packets.reserve(rtl_temporal_units.size());
        for (const auto& tu : rtl_temporal_units) {
            rtl_total += tu.payload.size();
            rtl_sequence_packets.push_back({tu.pts, tu.payload});
        }
        rtl_stream.reserve(rtl_total);
        for (const auto& tu : rtl_temporal_units)
            rtl_stream.insert(rtl_stream.end(), tu.payload.begin(), tu.payload.end());

        fs::path rtl_raw_path = output_dir / (output_path.stem().string() + "_rtl_raw.obu");
        std::ofstream rtl_out(rtl_raw_path, std::ios::binary | std::ios::trunc);
        if (rtl_out.is_open()) {
            rtl_out.write(reinterpret_cast<const char*>(rtl_stream.data()), rtl_stream.size());
            rtl_out.close();
            fprintf(stderr, "[TB] Wrote concatenated RTL raw stream: %zu bytes to %s\n",
                    rtl_stream.size(), rtl_raw_path.string().c_str());
        }

        fs::path rtl_ivf_path = output_dir / (output_path.stem().string() + "_rtl.ivf");
        auto rtl_ivf_sequence =
            AV1BitstreamWriter::write_ivf_sequence(FRAME_WIDTH, FRAME_HEIGHT, rtl_sequence_packets);
        std::ofstream rtl_ivf_out(rtl_ivf_path, std::ios::binary | std::ios::trunc);
        if (rtl_ivf_out.is_open()) {
            rtl_ivf_out.write(reinterpret_cast<const char*>(rtl_ivf_sequence.data()),
                              rtl_ivf_sequence.size());
            rtl_ivf_out.close();
            fprintf(stderr, "[TB] Wrote RTL sequence IVF: %zu bytes to %s\n",
                    rtl_ivf_sequence.size(), rtl_ivf_path.string().c_str());
        }
    }

    delete dut;
    return 0;
}
