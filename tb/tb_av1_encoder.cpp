#include <verilated.h>
#include "Vav1_encoder_top.h"
#include "Vav1_encoder_top___024root.h"  // Access to internal signals
#include "av1_bitstream_writer.h"
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    }

    namespace fs = std::filesystem;
    const fs::path output_path(output_file);
    const fs::path output_dir = output_path.has_parent_path() ? output_path.parent_path() : fs::current_path();
    const fs::path still_dir = output_dir / "still_frames";
    std::error_code fs_ec;
    fs::create_directories(output_dir, fs_ec);
    fs::remove_all(still_dir, fs_ec);
    fs::create_directories(still_dir, fs_ec);

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
            num_frames, FRAME_WIDTH, FRAME_HEIGHT, qindex,
            dc_only ? "DC-only" : "Full", all_key ? "all-key" : "IP");
    fprintf(stderr, "==========================================================\n");

    Vav1_encoder_top* dut = new Vav1_encoder_top;
    dut->clk = 0; dut->rst_n = 0; dut->start = 0;
    dut->frame_num_in = 0; dut->is_keyframe_in = 0;
    dut->qindex_in = qindex;
    dut->ref_mem_rd_data = 128;
    dut->chr_cb_ref_rd_data = 128;
    dut->chr_cr_ref_rd_data = 128;

    uint64_t cycle = 0;
    int frame_idx = 0;
    uint32_t total_bs_bytes = 0;
    bool frame_active = false;

    // FSM state constants (must match av1_encoder_top.v)
    constexpr int TS_REF_WR = 20;

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
            dut->qindex_in = qindex;
            frame_active = true;
            frame_blocks.clear();
            frame_blocks.resize(BLK_COLS * BLK_ROWS);
            last_captured_blk = -1;
            fprintf(stderr, "[TB] Frame %d (%s) start @ cycle %llu\n",
                    frame_idx, is_key ? "KEY" : "INTER", (unsigned long long)cycle);
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

        // Capture quantized coefficients when block enters TS_REF_WR
        // (at this point, all 64 coefficients have been quantized)
        {
            auto* root = dut->rootp;
            int state = root->av1_encoder_top__DOT__top_state;
            int bx = root->av1_encoder_top__DOT__blk_x;
            int by = root->av1_encoder_top__DOT__blk_y;
            int blk_idx = by * BLK_COLS + bx;

            if (state == TS_REF_WR && blk_idx != last_captured_blk) {
                last_captured_blk = blk_idx;
                if (blk_idx < (int)frame_blocks.size()) {
                    auto& bi = frame_blocks[blk_idx];
                    for (int i = 0; i < 64; i++) {
                        bi.qcoeff[i] = (int16_t)root->av1_encoder_top__DOT__qcoeff[i];
                    }
                    bi.pred_mode = root->av1_encoder_top__DOT__best_intra_mode;
                    bi.is_inter = false;
                }
            }
        }

        // Bitstream memory write
        if (dut->bs_mem_wr) {
            uint32_t addr = dut->bs_mem_addr;
            if (addr < bitstream_mem.size())
                bitstream_mem[addr] = dut->bs_mem_data;
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
            total_bs_bytes = dut->bs_bytes_written;
            fprintf(stderr, "[TB] Frame %d done @ cycle %llu -- rtl_bs_bytes=%u\n",
                    frame_idx, (unsigned long long)cycle, total_bs_bytes);

            // Build proper AV1 bitstream using captured coefficients
            {
                AV1BitstreamWriter writer(FRAME_WIDTH, FRAME_HEIGHT, qindex);
                writer.set_dc_only_mode(dc_only != 0);
                for (auto& bi : frame_blocks)
                    writer.add_block(bi);
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

    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "[TB] %d frames encoded, %llu cycles, rtl_bs=%u bytes\n",
            frame_idx, (unsigned long long)cycle, total_bs_bytes);
    fprintf(stderr, "==========================================================\n");

    // Also write the RTL bitstream (for debugging)
    std::ofstream out(output_file, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<char*>(bitstream_mem.data()), total_bs_bytes);
        out.close();
    }

    delete dut;
    return 0;
}
