#include <verilated.h>
#include "Vav1_encoder_top.h"
#include "Vav1_encoder_top___024root.h"  // Access to internal signals
#include "av1_bitstream_writer.h"
#include <algorithm>
#include <array>
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
#ifndef VERILATOR_THREADS
#define VERILATOR_THREADS 1
#endif
static constexpr int FRAME_WIDTH  = FRAME_W;
static constexpr int FRAME_HEIGHT = FRAME_H;
static constexpr int FRAME_SIZE   = FRAME_WIDTH * FRAME_HEIGHT * 3 / 2;
static constexpr int LUMA_SIZE    = FRAME_WIDTH * FRAME_HEIGHT;
static constexpr int CHROMA_SIZE  = (FRAME_WIDTH / 2) * (FRAME_HEIGHT / 2);
static constexpr int BLK_COLS     = FRAME_WIDTH / 8;
static constexpr int BLK_ROWS     = FRAME_HEIGHT / 8;
static constexpr size_t DEFAULT_MAX_BITSTREAM = 64 * 1024 * 1024;

// P9 disabled-filter / reference-ownership contract. The current low-delay
// subset deliberately signals loop_filter_level[0..1]=0, enable_cdef=0, and
// enable_restoration=0. Under that AV1 syntax, post-filter output equals the
// unfiltered RTL reconstruction, so the harness may promote ref_*_wr directly
// into LAST-reference read buffers. If any filter is enabled, this constant
// must be changed and the direct promotion below must fail until real RTL
// post-recon filter/restoration writeback exists.
static constexpr bool P9_POST_RECON_FILTERS_DISABLED = true;
static constexpr int P9_LOOP_FILTER_LEVEL_0 = 0;
static constexpr int P9_LOOP_FILTER_LEVEL_1 = 0;
static constexpr int P9_ENABLE_CDEF = 0;
static constexpr int P9_ENABLE_RESTORATION = 0;

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
static int16_t sign_extend_16(uint16_t v) {
    return static_cast<int16_t>(v);
}
static constexpr uint8_t REDUCED_INTER_NONE = 0;
static constexpr uint8_t REDUCED_INTER_GLOBALMV = 1;
static constexpr uint8_t REDUCED_INTER_NEARESTMV = 2;
static constexpr uint8_t REDUCED_INTER_NEARMV = 3;
static constexpr uint8_t REDUCED_INTER_NEWMV = 4;
static const char* reduced_inter_mode_name(uint8_t mode) {
    switch (mode) {
    case REDUCED_INTER_NONE: return "NONE";
    case REDUCED_INTER_GLOBALMV: return "GLOBALMV";
    case REDUCED_INTER_NEARESTMV: return "NEARESTMV";
    case REDUCED_INTER_NEARMV: return "NEARMV";
    case REDUCED_INTER_NEWMV: return "NEWMV";
    default: return "UNKNOWN";
    }
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

    int num_frames = 1;
    int qindex = 128;
    int dc_only = 1;
    int all_key = 1;
    std::string gop_mode;
    int key_interval = 12;
    int refresh_frame_flags = 0x01;
    int dump_ref_summary = 0;
    int enable_order_hint = 0;
    int order_hint_bits = 0;
    int dump_blocks = 0;
    int dump_coeff_summary = 0;
    int dump_partition = 0;
    int force_intra = 0;
    int me_zero_mv_only = 0;
    int me_newmv_limit = 0;
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
    int static_cdf_mode = 1;
    int trace_entropy = 0;
    int trace_bs = 0;
    int trace_entropy_shadow = 0;
    int trace_writer_entropy = 0;
    int dump_chroma_summary = 0;
    int dump_chroma_detail = 0;
    int dump_chroma_detail_start = -1;
    int dump_chroma_detail_end = -1;
    int ownership_strict = 0;
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
        else if (arg.rfind("+gop_mode=", 0) == 0) gop_mode = arg.substr(std::strlen("+gop_mode="));
        else if (arg.rfind("+key_interval=", 0) == 0) key_interval = std::atoi(arg.c_str() + std::strlen("+key_interval="));
        else if (arg.rfind("+refresh_policy=", 0) == 0) {
            std::string refresh_policy = arg.substr(std::strlen("+refresh_policy="));
            if (refresh_policy != "last_only") {
                fprintf(stderr,
                        "[TB] ERROR: refresh_policy=%s is not supported in this P8-A subset; use last_only\n",
                        refresh_policy.c_str());
                return 1;
            }
        }
        else if (arg.rfind("+refresh_frame_flags=", 0) == 0) refresh_frame_flags = std::strtoul(arg.c_str() + std::strlen("+refresh_frame_flags="), nullptr, 0);
        else if (arg.rfind("+dump_ref_summary=", 0) == 0) dump_ref_summary = std::atoi(arg.c_str() + std::strlen("+dump_ref_summary="));
        else if (arg.rfind("+enable_order_hint=", 0) == 0) enable_order_hint = std::atoi(arg.c_str() + std::strlen("+enable_order_hint="));
        else if (arg.rfind("+order_hint_bits=", 0) == 0) order_hint_bits = std::atoi(arg.c_str() + std::strlen("+order_hint_bits="));
        else if (arg.rfind("+dump_blocks=", 0) == 0) dump_blocks = std::atoi(arg.c_str() + 13);
        else if (arg.rfind("+dump_coeff_summary=", 0) == 0) dump_coeff_summary = std::atoi(arg.c_str() + 20);
        else if (arg.rfind("+dump_partition=", 0) == 0) dump_partition = std::atoi(arg.c_str() + 16);
        else if (arg.rfind("+force_intra=", 0) == 0) force_intra = std::atoi(arg.c_str() + 13);
        else if (arg.rfind("+me_zero_mv_only=", 0) == 0) me_zero_mv_only = std::atoi(arg.c_str() + 17);
        else if (arg.rfind("+me_newmv_limit=", 0) == 0) me_newmv_limit = std::atoi(arg.c_str() + 16);
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
        } else if (arg.rfind("+dump_chroma_summary=", 0) == 0) {
            dump_chroma_summary = std::atoi(arg.c_str() + 21);
        } else if (arg.rfind("+dump_chroma_detail=", 0) == 0) {
            dump_chroma_detail = std::atoi(arg.c_str() + std::strlen("+dump_chroma_detail="));
        } else if (arg.rfind("+dump_chroma_detail_start=", 0) == 0) {
            dump_chroma_detail_start = std::atoi(arg.c_str() + std::strlen("+dump_chroma_detail_start="));
        } else if (arg.rfind("+dump_chroma_detail_end=", 0) == 0) {
            dump_chroma_detail_end = std::atoi(arg.c_str() + std::strlen("+dump_chroma_detail_end="));
        } else if (arg.rfind("+ownership_strict=", 0) == 0) {
            ownership_strict = std::atoi(arg.c_str() + 18);
        } else if (arg.rfind("+progress_every=", 0) == 0) {
            progress_every = std::strtoull(arg.c_str() + 16, nullptr, 10);
        }
    }

    if (gop_mode.empty()) gop_mode = all_key ? "all_key" : "lowdelay_last";
    if (gop_mode != "all_key" && gop_mode != "lowdelay_last") {
        fprintf(stderr, "[TB] ERROR: unsupported gop_mode=%s\n", gop_mode.c_str());
        return 1;
    }
    const bool gop_all_key = (gop_mode == "all_key");
    if (gop_all_key) key_interval = 1;
    if (key_interval < 1) {
        fprintf(stderr, "[TB] ERROR: key_interval must be >= 1\n");
        return 1;
    }
    if (refresh_frame_flags != 0x01) {
        fprintf(stderr, "[TB] ERROR: refresh_frame_flags=0x%x is not supported in this P8-A subset; use 0x01\n",
                refresh_frame_flags);
        return 1;
    }
    if (enable_order_hint != 0 || order_hint_bits != 0) {
        fprintf(stderr,
                "[TB] ERROR: order hints remain disabled in this P8-A subset; keep enable_order_hint/order_hint_bits at 0\n");
        return 1;
    }

    const int effective_qindex = qindex <= 0 ? 1 : qindex;


    if (ownership_strict) {
        bool strict_ok = true;
        auto reject_strict_arg = [&](bool bad, const char* name, const char* reason) {
            if (bad) {
                fprintf(stderr,
                        "[TB][OWNERSHIP_STRICT][FATAL] %s is not allowed in RTL-owned proof mode: %s\n",
                        name, reason);
                strict_ok = false;
            }
        };

        reject_strict_arg(zero_inter_coeffs != 0, "+zero_inter_coeffs",
                          "writer-only coefficient repair would hide RTL residual syntax");
        reject_strict_arg(limit_newmv_blocks >= 0, "+limit_newmv_blocks",
                          "writer-only MV reduction; use RTL +me_newmv_limit for constrained proofs");
        reject_strict_arg(limit_inter_blocks >= 0, "+limit_inter_blocks",
                          "writer-only inter/intra rewrite of captured block decisions");
        reject_strict_arg(override_first_newmv != 0, "+override_first_newmvx/y",
                          "writer-only MV override of captured RTL decisions");
        reject_strict_arg(only_full_coeff_block >= 0, "+only_full_coeff_block",
                          "writer-only coefficient zeroing outside one debug block");
        reject_strict_arg(max_coeff_block >= 0, "+max_coeff_block",
                          "writer-only coefficient truncation after a block index");
        reject_strict_arg(force_first_ac_positive != 0, "+force_first_ac_positive",
                          "writer-only coefficient sign repair");
        reject_strict_arg(force_first_ac_to_scan1 != 0, "+force_first_ac_to_scan1",
                          "writer-only coefficient scan-position repair");
        reject_strict_arg(coeff_debug != 0, "+coeff_debug",
                          "debug coefficient writer mode is not an ownership proof");
        reject_strict_arg(max_scan_coeffs >= 0, "+max_scan_coeffs",
                          "writer-only coefficient scan truncation");
        reject_strict_arg(debug_zero_coeff_block >= 0 || debug_zero_coeff_idx >= 0,
                          "+debug_zero_coeff_block/idx",
                          "writer-only coefficient deletion");
        reject_strict_arg(debug_transpose_coeff_block >= 0, "+debug_transpose_coeff_block",
                          "writer-only coefficient transposition");
        reject_strict_arg(debug_add_coeff_block >= 0 || debug_add_coeff_idx >= 0 || debug_add_coeff_delta != 0,
                          "+debug_add_coeff_block/idx/delta",
                          "writer-only coefficient injection");
        reject_strict_arg(static_cdf_mode == 0, "+static_cdf_mode=0",
                          "adaptive CDF update is not RTL-owned in this reduced subset");

        if (!strict_ok) {
            return 1;
        }
        fprintf(stderr,
                "[TB][OWNERSHIP_STRICT] enabled: decoder proof must use encoded_rtl_raw.obu/encoded_rtl.ivf; "
                "the C++ writer is oracle/debug only, repair knobs are disabled, and static CDF mode is enforced.\n");
    }

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
    fprintf(stderr, "  Frames: %d  Resolution: %dx%d  QIndex: %d  CoeffMode: %s  GOP: %s  key_interval=%d\n",
            num_frames, FRAME_WIDTH, FRAME_HEIGHT, effective_qindex,
            dc_only ? "DC-only" : "Full", gop_mode.c_str(), key_interval);
    fprintf(stderr,
            "[TB] GOP control: gop_mode=%s key_interval=%d inter_refresh_frame_flags=0x%02x ref_map=LASTx7 order_hint=disabled\n",
            gop_mode.c_str(), key_interval, refresh_frame_flags);
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

    VerilatedContext context;
    context.commandArgs(argc, argv);
    context.threads(VERILATOR_THREADS);
    Vav1_encoder_top* dut = new Vav1_encoder_top{&context};
    dut->clk = 0; dut->rst_n = 0; dut->start = 0;
    dut->frame_num_in = 0; dut->is_keyframe_in = 0;
    dut->force_intra_in = force_intra ? 1 : 0;
    dut->me_zero_mv_only_in = me_zero_mv_only ? 1 : 0;
    dut->me_newmv_limit_in = (me_newmv_limit < 0) ? 0 : (me_newmv_limit > 255 ? 255 : me_newmv_limit);
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
    int current_frame_gop_pos = 0;
    uint8_t current_frame_refresh_frame_flags = 0x01;
    const char* current_frame_source_ref = "NONE";
    std::vector<EncodedTemporalUnit> temporal_units;
    std::vector<EncodedTemporalUnit> rtl_temporal_units;
    std::vector<uint8_t> rtl_byte_stream;
    std::vector<std::array<uint8_t, 16>> frame_cb_pred_dbg;
    std::vector<std::array<uint8_t, 16>> frame_cr_pred_dbg;
    std::vector<std::array<uint8_t, 16>> frame_cb_recon_dbg;
    std::vector<std::array<uint8_t, 16>> frame_cr_recon_dbg;
    std::vector<PendingEntropyOp> entropy_req_log;
    std::vector<PendingEntropyOp> entropy_accept_log;
    std::vector<uint8_t> entropy_byte_log;
    AV1RangeCoder entropy_live_shadow;
    bool entropy_live_shadow_valid = false;
    bool entropy_state_mismatch = false;
    uint64_t chroma_inter_prev_cb_reads = 0;
    uint64_t chroma_inter_prev_cr_reads = 0;
    uint64_t chroma_neigh_cb_reads = 0;
    uint64_t chroma_neigh_cr_reads = 0;
    uint64_t next_progress_cycle = 0;

    // FSM state constants (must match av1_encoder_top.v)
    constexpr int TS_PREDICT = 11;
    constexpr int TS_WAIT_PRED = 12;
    constexpr int TS_NEXT_BLK = 19;
    constexpr int TS_REF_WR = 20;
    constexpr int TS_CHR_FETCH = 21;
    constexpr int TS_CHR_WAIT = 30;
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
            current_frame_gop_pos = gop_all_key ? 0 : (frame_idx % key_interval);
            bool is_key = gop_all_key ? true : (current_frame_gop_pos == 0);
            dut->frame_num_in = gop_all_key ? 0 : (current_frame_gop_pos & 0xF);
            dut->is_keyframe_in = is_key ? 1 : 0;
            dut->refresh_frame_flags_in = is_key ? 0xFF : static_cast<uint8_t>(refresh_frame_flags);
            dut->ref_frame_idx_map_in = 0;
            dut->force_intra_in = force_intra ? 1 : 0;
            dut->me_zero_mv_only_in = me_zero_mv_only ? 1 : 0;
            dut->me_newmv_limit_in = (me_newmv_limit < 0) ? 0 : (me_newmv_limit > 255 ? 255 : me_newmv_limit);
            dut->dc_only_in = dc_only ? 1 : 0;
            dut->qindex_in = effective_qindex;
            current_frame_is_key = is_key;
            current_frame_refresh_frame_flags = is_key ? 0xFF : static_cast<uint8_t>(refresh_frame_flags);
            current_frame_source_ref = is_key ? "NONE" : "LAST";
            frame_active = true;
            frame_blocks.clear();
            frame_blocks.resize(BLK_COLS * BLK_ROWS);
            frame_cb_pred_dbg.assign(BLK_COLS * BLK_ROWS, {});
            frame_cr_pred_dbg.assign(BLK_COLS * BLK_ROWS, {});
            frame_cb_recon_dbg.assign(BLK_COLS * BLK_ROWS, {});
            frame_cr_recon_dbg.assign(BLK_COLS * BLK_ROWS, {});
            last_captured_blk = -1;
            rtl_byte_stream.clear();
            entropy_req_log.clear();
            entropy_accept_log.clear();
            entropy_byte_log.clear();
            entropy_live_shadow.init();
            entropy_live_shadow_valid = true;
            entropy_state_mismatch = false;
            chroma_inter_prev_cb_reads = 0;
            chroma_inter_prev_cr_reads = 0;
            chroma_neigh_cb_reads = 0;
            chroma_neigh_cr_reads = 0;
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

        // Chroma reference reads.  Inter prediction reads the previous frame;
        // intra chroma DC prediction reads already-reconstructed current-frame
        // chroma neighbors, mirroring the luma ref_rd_is_neigh path.
        {
            uint32_t addr = dut->chr_cb_ref_rd_addr;
            const auto& cb_src = dut->chr_ref_rd_is_neigh ? ref_cb_wr : ref_cb_rd;
            dut->chr_cb_ref_rd_data = (addr < cb_src.size()) ? cb_src[addr] : 128;
        }
        {
            uint32_t addr = dut->chr_cr_ref_rd_addr;
            const auto& cr_src = dut->chr_ref_rd_is_neigh ? ref_cr_wr : ref_cr_rd;
            dut->chr_cr_ref_rd_data = (addr < cr_src.size()) ? cr_src[addr] : 128;
        }

        dut->eval();
        if (dut->start) dut->start = 0;
        {
            auto* root = dut->rootp;
            const int state = root->av1_encoder_top__DOT__top_state;
            if (state == TS_CHR_WAIT) {
                if (dut->chr_ref_rd_is_neigh) {
                    if (root->av1_encoder_top__DOT__chr_plane)
                        ++chroma_neigh_cr_reads;
                    else
                        ++chroma_neigh_cb_reads;
                } else if (root->av1_encoder_top__DOT__use_inter && !current_frame_is_key) {
                    if (root->av1_encoder_top__DOT__chr_plane)
                        ++chroma_inter_prev_cr_reads;
                    else
                        ++chroma_inter_prev_cb_reads;
                }
            }
        }
        if (progress_every && frame_active && cycle >= next_progress_cycle) {
            auto* root = dut->rootp;
            fprintf(stderr,
                    "[TB] progress frame=%d/%d cycle=%llu state=%d blk=(%d,%d) key=%d force_intra=%d use_inter=%d me_mv_q3=(%d,%d) done=%d "
                    "chr_res_state=%d row=%d col=%d proc=%d fetch_done=%d chr_res_done=%d xform_done=%d quant_done=%d iq_done=%d inv_done=%d "
                    "qstart=%d qstage=%d qisdc=%d qcoeff_in=%d qdeq=%d\n",
                    frame_idx, num_frames,
                    (unsigned long long)cycle,
                    root->av1_encoder_top__DOT__top_state,
                    root->av1_encoder_top__DOT__blk_x,
                    root->av1_encoder_top__DOT__blk_y,
                    dut->is_keyframe_in ? 1 : 0,
                    dut->force_intra_in ? 1 : 0,
                    root->av1_encoder_top__DOT__use_inter ? 1 : 0,
                    sign_extend_16(root->av1_encoder_top__DOT__me_mvx_q3),
                    sign_extend_16(root->av1_encoder_top__DOT__me_mvy_q3),
                    dut->done ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__state,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__row_idx,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__col_idx,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__proc_idx,
                    root->av1_encoder_top__DOT__fetch_done ? 1 : 0,
                    root->av1_encoder_top__DOT__chroma_res_done ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__xform_done ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__quant_done ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__iq_done ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__inv_done ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__quant_start ? 1 : 0,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__u_quantize__DOT__stage,
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__quant_is_dc ? 1 : 0,
                    sign_extend_16(root->av1_encoder_top__DOT__u_chroma_residual__DOT__quant_coeff_in),
                    root->av1_encoder_top__DOT__u_chroma_residual__DOT__u_quantize__DOT__dequant);
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

            if (dump_partition && state == 133 &&
                dut->ec_dbg_accept_valid_out && dut->ec_dbg_accept_kind_out == 2) {
                fprintf(stderr,
                        "[P4_PART] frame=%d blk=(%d,%d) log2=%u symbol=%u nsyms=%u\n",
                        frame_idx, bx, by,
                        static_cast<unsigned>(root->av1_encoder_top__DOT__part_level_log2),
                        static_cast<unsigned>(dut->ec_dbg_accept_symbol_out),
                        static_cast<unsigned>(dut->ec_dbg_accept_nsyms_out));
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
                    bi.inter_mode = bi.is_inter ? root->av1_encoder_top__DOT__cur_reduced_inter_mode
                                                : REDUCED_INTER_NONE;
                    bi.mode_ctx = bi.is_inter ? root->av1_encoder_top__DOT__cur_mode_ctx : 0;
                    bi.ref_mvx = 0;
                    bi.ref_mvy = 0;
                    bi.near_mvx = 0;
                    bi.near_mvy = 0;
                    bi.mv_cand_count = 0;
                    for (int ci = 0; ci < 10; ++ci) {
                        bi.mv_cand_mvx[ci] = 0;
                        bi.mv_cand_mvy[ci] = 0;
                        bi.mv_cand_weight[ci] = 0;
                    }
                    if (bi.is_inter) {
                        const int cand_count = root->av1_encoder_top__DOT__cur_ref_mv_count;
                        bi.mv_cand_count = static_cast<uint8_t>(cand_count < 0 ? 0 : (cand_count > 10 ? 10 : cand_count));
                        int best_idx = -1;
                        int second_idx = -1;
                        for (int ci = 0; ci < cand_count && ci < 10; ++ci) {
                            bi.mv_cand_mvx[ci] = sign_extend_16(root->av1_encoder_top__DOT__cur_mv_cand_col[ci]);
                            bi.mv_cand_mvy[ci] = sign_extend_16(root->av1_encoder_top__DOT__cur_mv_cand_row[ci]);
                            bi.mv_cand_weight[ci] = root->av1_encoder_top__DOT__cur_mv_cand_weight[ci];
                            if (best_idx < 0 ||
                                root->av1_encoder_top__DOT__cur_mv_cand_weight[ci] > root->av1_encoder_top__DOT__cur_mv_cand_weight[best_idx]) {
                                second_idx = best_idx;
                                best_idx = ci;
                            } else if (second_idx < 0 ||
                                       root->av1_encoder_top__DOT__cur_mv_cand_weight[ci] > root->av1_encoder_top__DOT__cur_mv_cand_weight[second_idx]) {
                                second_idx = ci;
                            }
                        }
                        if (best_idx >= 0) {
                            bi.ref_mvx = sign_extend_16(root->av1_encoder_top__DOT__cur_mv_cand_col[best_idx]);
                            bi.ref_mvy = sign_extend_16(root->av1_encoder_top__DOT__cur_mv_cand_row[best_idx]);
                            bi.near_mvx = bi.ref_mvx;
                            bi.near_mvy = bi.ref_mvy;
                        }
                        if (second_idx >= 0) {
                            bi.near_mvx = sign_extend_16(root->av1_encoder_top__DOT__cur_mv_cand_col[second_idx]);
                            bi.near_mvy = sign_extend_16(root->av1_encoder_top__DOT__cur_mv_cand_row[second_idx]);
                        }
                    }
                    bi.mvx = sign_extend_16(root->av1_encoder_top__DOT__me_mvx_q3);
                    bi.mvy = sign_extend_16(root->av1_encoder_top__DOT__me_mvy_q3);
                }
            }

            if (state == TS_NEXT_BLK && blk_idx >= 0 && blk_idx < (int)frame_blocks.size()) {
                auto& bi = frame_blocks[blk_idx];
                for (int i = 0; i < 16; i++) {
                    bi.cb_qcoeff[i] = (int16_t)root->av1_encoder_top__DOT__chr_cb_qcoeff[i];
                    bi.cr_qcoeff[i] = (int16_t)root->av1_encoder_top__DOT__chr_cr_qcoeff[i];
                }
                bi.cb_has_coeff = root->av1_encoder_top__DOT__chr_cb_has_coeff;
                bi.cr_has_coeff = root->av1_encoder_top__DOT__chr_cr_has_coeff;
                for (int i = 0; i < 16; i++) {
                    frame_cb_pred_dbg[blk_idx][i] = root->av1_encoder_top__DOT__chr_cb_pred_dbg[i];
                    frame_cr_pred_dbg[blk_idx][i] = root->av1_encoder_top__DOT__chr_cr_pred_dbg[i];
                    frame_cb_recon_dbg[blk_idx][i] = root->av1_encoder_top__DOT__chr_cb_recon_dbg[i];
                    frame_cr_recon_dbg[blk_idx][i] = root->av1_encoder_top__DOT__chr_cr_recon_dbg[i];
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
            if (dump_ref_summary) {
                fprintf(stderr,
                        "[TB] ref_summary frame=%d mode=%s gop_mode=%s key_interval=%d gop_pos=%d frame_num=%d source_ref=%s refresh=0x%02x last_ref_rd=LAST last_ref_wr=LAST ref_map=0,0,0,0,0,0,0\n",
                        frame_idx, current_frame_is_key ? "KEY" : "INTER", gop_mode.c_str(), key_interval,
                        current_frame_gop_pos, dut->frame_num_in, current_frame_source_ref,
                        current_frame_refresh_frame_flags);
            }
            {
                const size_t rtl_bytes = rtl_byte_stream.size();
                if (rtl_bytes != static_cast<size_t>(total_bs_bytes)) {
                    fprintf(stderr,
                            "[TB] RTL byte capture size mismatch: direct=%zu bs_bytes_written=%u\n",
                            rtl_bytes, total_bs_bytes);
                    if (ownership_strict) {
                        fprintf(stderr,
                                "[TB][OWNERSHIP_STRICT][FATAL] capture mismatch would require truncation/padding; refusing to write proof artifacts\n");
                        delete dut;
                        return 1;
                    }
                }
                const size_t rtl_copy_bytes = ownership_strict
                                                  ? static_cast<size_t>(total_bs_bytes)
                                                  : std::min(rtl_byte_stream.size(),
                                                             static_cast<size_t>(total_bs_bytes));
                std::vector<uint8_t> rtl_frame_payload(
                    rtl_byte_stream.begin(),
                    rtl_byte_stream.begin() + rtl_copy_bytes);
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
                writer.set_refresh_frame_flags(current_frame_refresh_frame_flags);
                writer.set_ref_frame_idx_map_last_only();
                if (!gop_all_key) {
                    writer.set_still_picture_mode(false);
                    writer.set_include_sequence_header(true);
                    writer.set_force_video_intra_only(false);
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
                writer.set_refresh_frame_flags(current_frame_refresh_frame_flags);
                writer.set_ref_frame_idx_map_last_only();
                writer.set_still_picture_mode(false);
                writer.set_include_sequence_header(frame_idx == 0);
                writer.set_force_video_intra_only(false);
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
                    int cb_nonzero = 0;
                    int cr_nonzero = 0;
                    for (int i = 0; i < 16; ++i) {
                        if (bi.cb_qcoeff[i] != 0) ++cb_nonzero;
                        if (bi.cr_qcoeff[i] != 0) ++cr_nonzero;
                    }
                    const bool luma_has = nonzero != 0;
                    const bool cb_has = bi.cb_has_coeff || cb_nonzero != 0;
                    const bool cr_has = bi.cr_has_coeff || cr_nonzero != 0;
                    const bool block_skip = !(luma_has || cb_has || cr_has);
                    fprintf(stderr,
                            "[P4_CBP] frame=%d blk=%zu luma=%d cb=%d cr=%d skip=%d luma_nz=%d cb_nz=%d cr_nz=%d\n",
                            frame_idx, bi_idx, luma_has ? 1 : 0, cb_has ? 1 : 0,
                            cr_has ? 1 : 0, block_skip ? 1 : 0,
                            nonzero, cb_nonzero, cr_nonzero);

                    if (!nonzero && bi.pred_mode == 0 && !bi.is_inter && !cb_has && !cr_has) continue;

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

            if (dump_coeff_summary) {
                for (size_t bi_idx = 0; bi_idx < frame_blocks.size(); ++bi_idx) {
                    const auto& bi = frame_blocks[bi_idx];
                    int eob = 0;
                    int nz = 0;
                    int ac_nz = 0;
                    int first_ac_scan = -1;
                    int max_abs = 0;
                    for (int scan_idx = 0; scan_idx < 64; ++scan_idx) {
                        const int pos = default_scan_8x8[scan_idx];
                        int coeff = (dc_only != 0 && scan_idx > 0) ? 0 : bi.qcoeff[pos];
                        int abs_coeff = coeff < 0 ? -coeff : coeff;
                        if (abs_coeff > max_abs) max_abs = abs_coeff;
                        if (coeff == 0) continue;
                        ++nz;
                        eob = scan_idx + 1;
                        if (scan_idx > 0) {
                            ++ac_nz;
                            if (first_ac_scan < 0) first_ac_scan = scan_idx;
                        }
                    }
                    const int dc = bi.qcoeff[0];
                    const int abs_dc = dc < 0 ? -dc : dc;
                    fprintf(stderr,
                            "[P5_COEFF] frame=%d blk=%zu eob=%d first_ac_scan=%d nz=%d ac_nz=%d dc=%d abs_dc=%d max_abs=%d\n",
                            frame_idx, bi_idx, eob, first_ac_scan, nz, ac_nz, dc, abs_dc, max_abs);
                }
            }

            if (dump_chroma_summary) {
                int cb_nonzero_blocks = 0;
                int cr_nonzero_blocks = 0;
                int cb_nonzero_coeffs = 0;
                int cr_nonzero_coeffs = 0;
                int cb_inter_nonzero_blocks = 0;
                int cr_inter_nonzero_blocks = 0;
                int chroma_only_blocks = 0;
                for (size_t bi_idx = 0; bi_idx < frame_blocks.size(); ++bi_idx) {
                    const auto& bi = frame_blocks[bi_idx];
                    int luma_nz = 0;
                    int cb_nz = 0;
                    int cr_nz = 0;
                    for (int i = 0; i < 64; ++i) {
                        if (bi.qcoeff[i] != 0) ++luma_nz;
                    }
                    for (int i = 0; i < 16; ++i) {
                        if (bi.cb_qcoeff[i] != 0) ++cb_nz;
                        if (bi.cr_qcoeff[i] != 0) ++cr_nz;
                    }
                    if (cb_nz) {
                        ++cb_nonzero_blocks;
                        cb_nonzero_coeffs += cb_nz;
                        if (bi.is_inter) ++cb_inter_nonzero_blocks;
                    }
                    if (cr_nz) {
                        ++cr_nonzero_blocks;
                        cr_nonzero_coeffs += cr_nz;
                        if (bi.is_inter) ++cr_inter_nonzero_blocks;
                    }
                    if (luma_nz == 0 && (cb_nz || cr_nz))
                        ++chroma_only_blocks;
                    const bool chroma_detail_in_range =
                        (dump_chroma_detail_start < 0 || static_cast<int>(bi_idx) >= dump_chroma_detail_start) &&
                        (dump_chroma_detail_end < 0 || static_cast<int>(bi_idx) <= dump_chroma_detail_end);
                    if (dump_chroma_detail && chroma_detail_in_range &&
                        (cb_nz || cr_nz || bi.cb_has_coeff || bi.cr_has_coeff)) {
                        fprintf(stderr,
                                "[TB] chroma_detail frame=%d blk=%zu inter=%d mv=(%d,%d) cb_has=%d cr_has=%d cb_nz=%d cr_nz=%d cb_qcoeff=",
                                frame_idx, bi_idx, bi.is_inter ? 1 : 0, bi.mvx, bi.mvy,
                                bi.cb_has_coeff ? 1 : 0, bi.cr_has_coeff ? 1 : 0,
                                cb_nz, cr_nz);
                        for (int qi = 0; qi < 16; ++qi) {
                            fprintf(stderr, "%s%d", qi ? "," : "", bi.cb_qcoeff[qi]);
                        }
                        fprintf(stderr, " cr_qcoeff=");
                        for (int qi = 0; qi < 16; ++qi) {
                            fprintf(stderr, "%s%d", qi ? "," : "", bi.cr_qcoeff[qi]);
                        }
                        fprintf(stderr, "\n");
                        fprintf(stderr,
                                "[TB] chroma_pixel_detail frame=%d blk=%zu cb_pred=",
                                frame_idx, bi_idx);
                        for (int qi = 0; qi < 16; ++qi) {
                            fprintf(stderr, "%s%u", qi ? "," : "", frame_cb_pred_dbg[bi_idx][qi]);
                        }
                        fprintf(stderr, " cb_recon=");
                        for (int qi = 0; qi < 16; ++qi) {
                            fprintf(stderr, "%s%u", qi ? "," : "", frame_cb_recon_dbg[bi_idx][qi]);
                        }
                        fprintf(stderr, " cr_pred=");
                        for (int qi = 0; qi < 16; ++qi) {
                            fprintf(stderr, "%s%u", qi ? "," : "", frame_cr_pred_dbg[bi_idx][qi]);
                        }
                        fprintf(stderr, " cr_recon=");
                        for (int qi = 0; qi < 16; ++qi) {
                            fprintf(stderr, "%s%u", qi ? "," : "", frame_cr_recon_dbg[bi_idx][qi]);
                        }
                        fprintf(stderr, "\n");
                    }
                }
                fprintf(stderr,
                        "[TB] chroma_summary frame=%d cb_nonzero_blocks=%d cr_nonzero_blocks=%d "
                        "cb_nonzero_coeffs=%d cr_nonzero_coeffs=%d "
                        "cb_inter_nonzero_blocks=%d cr_inter_nonzero_blocks=%d chroma_only_blocks=%d "
                        "inter_prev_cb_reads=%llu inter_prev_cr_reads=%llu neigh_cb_reads=%llu neigh_cr_reads=%llu\n",
                        frame_idx,
                        cb_nonzero_blocks, cr_nonzero_blocks,
                        cb_nonzero_coeffs, cr_nonzero_coeffs,
                        cb_inter_nonzero_blocks, cr_inter_nonzero_blocks,
                        chroma_only_blocks,
                        (unsigned long long)chroma_inter_prev_cb_reads,
                        (unsigned long long)chroma_inter_prev_cr_reads,
                        (unsigned long long)chroma_neigh_cb_reads,
                        (unsigned long long)chroma_neigh_cr_reads);
            }

            if (dump_inter_summary) {
                int inter_count = 0;
                int nonzero_inter_count = 0;
                int first_inter_idx = -1;
                int globalmv_count = 0;
                int nearestmv_count = 0;
                int nearmv_count = 0;
                int newmv_count = 0;
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
                    switch (bi.inter_mode) {
                    case REDUCED_INTER_GLOBALMV: ++globalmv_count; break;
                    case REDUCED_INTER_NEARESTMV: ++nearestmv_count; break;
                    case REDUCED_INTER_NEARMV: ++nearmv_count; break;
                    case REDUCED_INTER_NEWMV: ++newmv_count; break;
                    default: break;
                    }
                    const unsigned mode_ctx = bi.mode_ctx;
                    const unsigned newmv_ctx = mode_ctx & AV1_NEWMV_CTX_MASK;
                    const unsigned zeromv_ctx =
                        (mode_ctx >> AV1_GLOBALMV_OFFSET) & AV1_GLOBALMV_CTX_MASK;
                    const unsigned refmv_ctx =
                        (mode_ctx >> AV1_REFMV_OFFSET) & AV1_REFMV_CTX_MASK;
                    fprintf(stderr,
                            "[TB] inter_summary frame=%d blk=%zu mv=(%d,%d) ref=(%d,%d) near=(%d,%d) mode=%s mode_ctx=%u ctx(new=%u zero=%u ref=%u) dc=%d nz=%d",
                            frame_idx, bi_idx, bi.mvx, bi.mvy,
                            bi.ref_mvx, bi.ref_mvy, bi.near_mvx, bi.near_mvy,
                            reduced_inter_mode_name(bi.inter_mode), mode_ctx,
                            newmv_ctx, zeromv_ctx, refmv_ctx, bi.qcoeff[0], nonzero);
                    if (bi.mv_cand_count > 0) {
                        fprintf(stderr, " cand_count=%u", bi.mv_cand_count);
                        for (uint8_t ci = 0; ci < bi.mv_cand_count && ci < 10; ++ci) {
                            fprintf(stderr, " cand%u=(%d,%d,w=%u)",
                                    ci, bi.mv_cand_mvx[ci], bi.mv_cand_mvy[ci],
                                    bi.mv_cand_weight[ci]);
                        }
                    }
                    fprintf(stderr, "\n");
                }
                fprintf(stderr,
                        "[TB] inter_summary frame=%d total_inter=%d nonzero_inter=%d first_inter_blk=%d mode_counts={GLOBALMV:%d NEARESTMV:%d NEARMV:%d NEWMV:%d}\n",
                        frame_idx, inter_count, nonzero_inter_count, first_inter_idx,
                        globalmv_count, nearestmv_count, nearmv_count, newmv_count);
            }
            if (!P9_POST_RECON_FILTERS_DISABLED ||
                P9_LOOP_FILTER_LEVEL_0 != 0 || P9_LOOP_FILTER_LEVEL_1 != 0 ||
                P9_ENABLE_CDEF != 0 || P9_ENABLE_RESTORATION != 0) {
                std::fprintf(stderr,
                             "[TB] ERROR: ref promotion writes unfiltered reconstruction, "
                             "but P9 disabled-filter policy is not active. Add RTL post-filter "
                             "writeback before promoting references.\n");
                delete dut;
                return 1;
            }
            if (frame_idx == 0) {
                std::fprintf(stderr,
                             "[TB] P9 disabled-filter policy active: loop_filter_level[0..1]=0, "
                             "enable_cdef=0, enable_restoration=0; promoting unfiltered RTL "
                             "reconstruction as the LAST reference.\n");
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
                "use_inter=%d me_mv_q3=(%d,%d) inter_fetch_idx=%d done=%d\n",
                frame_idx, num_frames,
                (unsigned long long)cycle,
                (unsigned long long)timeout_cycles,
                root->av1_encoder_top__DOT__top_state,
                root->av1_encoder_top__DOT__blk_x,
                root->av1_encoder_top__DOT__blk_y,
                root->av1_encoder_top__DOT__use_inter ? 1 : 0,
                sign_extend_16(root->av1_encoder_top__DOT__me_mvx_q3),
                sign_extend_16(root->av1_encoder_top__DOT__me_mvy_q3),
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
            if (ownership_strict) {
                fs::path sw_oracle_obu_path = output_dir / (output_path.stem().string() + "_sw_oracle.obu");
                std::ofstream sw_obu_out(sw_oracle_obu_path, std::ios::binary | std::ios::trunc);
                if (sw_obu_out.is_open()) {
                    sw_obu_out.write(reinterpret_cast<const char*>(obu_stream.data()), obu_stream.size());
                    sw_obu_out.close();
                    fprintf(stderr, "[TB][OWNERSHIP_STRICT] Wrote software oracle OBU copy: %zu bytes to %s\n",
                            obu_stream.size(), sw_oracle_obu_path.string().c_str());
                }
            }
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
            if (ownership_strict) {
                fs::path sw_oracle_ivf_path = output_dir / (output_path.stem().string() + "_sw_oracle.ivf");
                std::ofstream sw_ivf_out(sw_oracle_ivf_path, std::ios::binary | std::ios::trunc);
                if (sw_ivf_out.is_open()) {
                    sw_ivf_out.write(reinterpret_cast<const char*>(ivf_sequence.data()),
                                     ivf_sequence.size());
                    sw_ivf_out.close();
                    fprintf(stderr, "[TB][OWNERSHIP_STRICT] Wrote software oracle IVF copy: %zu bytes to %s\n",
                            ivf_sequence.size(), sw_oracle_ivf_path.string().c_str());
                }
                fprintf(stderr,
                        "[TB][OWNERSHIP_STRICT] %s remains the software oracle; decode ownership proofs must use %s_rtl.ivf after raw/IVF integrity checks.\n",
                        seq_ivf_path.string().c_str(),
                        (output_dir / output_path.stem()).string().c_str());
            }
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
