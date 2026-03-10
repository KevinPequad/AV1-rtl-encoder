#include "av1_bitstream_writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "av1/common/scan.h"
#define clip_max3 aom_clip_max3_table
#define get_br_ctx_2d aom_get_br_ctx_2d
#include "av1/common/txb_common.h"
#undef get_br_ctx_2d
#undef clip_max3
#include "av1/encoder/encodetxb.h"
}

namespace {

constexpr int kTxDim = 8;
constexpr int kTxArea = 64;

int to_official_idx(int local_idx) {
    const int row = local_idx / kTxDim;
    const int col = local_idx % kTxDim;
    return col * kTxDim + row;
}

int to_local_idx(int official_idx) {
    const int col = official_idx / kTxDim;
    const int row = official_idx % kTxDim;
    return row * kTxDim + col;
}

std::array<tran_low_t, kTxArea> make_official_layout(const std::array<int16_t, kTxArea>& local_qcoeff) {
    std::array<tran_low_t, kTxArea> official_qcoeff{};
    for (int local_idx = 0; local_idx < kTxArea; ++local_idx) {
        official_qcoeff[to_official_idx(local_idx)] = local_qcoeff[local_idx];
    }
    return official_qcoeff;
}

void init_levels_local(const std::array<int16_t, kTxArea>& qcoeff, uint8_t* levels) {
    constexpr int stride = kTxDim + 4;
    std::memset(levels - 2 * stride, 0, 2 * stride);
    for (int row = 0; row < kTxDim; ++row) {
        for (int col = 0; col < kTxDim; ++col) {
            levels[row * stride + col] =
                static_cast<uint8_t>(std::min(std::abs(static_cast<int>(qcoeff[row * kTxDim + col])), 127));
        }
        for (int j = 0; j < 4; ++j) levels[row * stride + kTxDim + j] = 0;
    }
    std::memset(levels + kTxDim * stride, 0, (4 * stride) + 16);
}

int compute_eob_local(const std::array<int16_t, kTxArea>& qcoeff) {
    int eob = 0;
    for (int c = 0; c < kTxArea; ++c) {
        const int pos = default_scan_8x8[c];
        if (qcoeff[pos] != 0) eob = c + 1;
    }
    return eob;
}

void compare_case(const char* label, const std::array<int16_t, kTxArea>& local_qcoeff) {
    const TX_SIZE tx_size = TX_8X8;
    const TX_TYPE tx_type = DCT_DCT;
    const TX_CLASS tx_class = TX_CLASS_2D;
    const SCAN_ORDER* scan_order = get_scan(tx_size, tx_type);
    const int16_t* official_scan = scan_order->scan;
    const int bhl = get_txb_bhl(tx_size);

    const auto official_qcoeff = make_official_layout(local_qcoeff);
    const int local_eob = compute_eob_local(local_qcoeff);

    uint8_t local_levels_buf[12 * 14 + 16];
    std::memset(local_levels_buf, 0, sizeof(local_levels_buf));
    uint8_t* local_levels = local_levels_buf + 2 * 12;
    init_levels_local(local_qcoeff, local_levels);

    uint8_t official_levels_buf[TX_PAD_2D];
    uint8_t* official_levels = set_levels(official_levels_buf, get_txb_high(tx_size));
    std::memset(official_levels_buf, 0, sizeof(official_levels_buf));
    av1_txb_init_levels_c(official_qcoeff.data(), get_txb_wide(tx_size), get_txb_high(tx_size), official_levels);

    int official_eob = 0;
    for (int c = 0; c < kTxArea; ++c) {
        const int official_pos = official_scan[c];
        if (official_qcoeff[official_pos] != 0) official_eob = c + 1;
    }

    std::array<int8_t, kTxArea> official_coeff_ctx{};
    av1_get_nz_map_contexts_c(official_levels, official_scan, official_eob, tx_size, tx_class,
                              official_coeff_ctx.data());

    std::printf("%s\n", label);
    std::printf("  eob local=%d official=%d\n", local_eob, official_eob);

    bool scan_mismatch = false;
    for (int c = 0; c < std::max(local_eob, official_eob); ++c) {
        const int official_local_pos = to_local_idx(official_scan[c]);
        const int local_pos = default_scan_8x8[c];
        if (official_local_pos != local_pos) {
            scan_mismatch = true;
            std::printf("  scan mismatch c=%d local=%d official_local=%d\n", c, local_pos, official_local_pos);
            break;
        }
    }
    if (!scan_mismatch) std::printf("  scan order matches through eob\n");

    bool coeff_ctx_mismatch = false;
    bool br_ctx_mismatch = false;
    for (int c = 0; c < official_eob; ++c) {
        const int local_pos = to_local_idx(official_scan[c]);
        int local_ctx;
        if (c == official_eob - 1) {
            if (c == 0)
                local_ctx = 0;
            else if (c <= kTxArea / 8)
                local_ctx = 1;
            else if (c <= kTxArea / 4)
                local_ctx = 2;
            else
                local_ctx = 3;
        } else {
            local_ctx = get_nz_map_ctx(local_levels, local_pos, 3);
        }
        const int official_ctx = official_coeff_ctx[official_scan[c]];
        if (local_ctx != official_ctx) {
            coeff_ctx_mismatch = true;
            std::printf("  coeff_ctx mismatch c=%d pos=%d local=%d official=%d level=%d\n",
                        c, local_pos, local_ctx, official_ctx, std::abs(local_qcoeff[local_pos]));
            break;
        }
    }
    if (!coeff_ctx_mismatch) std::printf("  coeff contexts match through eob\n");

    for (int c = official_eob - 1; c >= 0; --c) {
        const int official_pos = official_scan[c];
        const int local_pos = to_local_idx(official_pos);
        const int level = std::abs(static_cast<int>(local_qcoeff[local_pos]));
        if (level <= NUM_BASE_LEVELS) continue;
        const int local_br_ctx = get_br_ctx_2d(local_levels, local_pos, 3);
        const int official_br_ctx = get_br_ctx(official_levels, official_pos, bhl, tx_class);
        if (local_br_ctx != official_br_ctx) {
            br_ctx_mismatch = true;
            std::printf("  br_ctx mismatch c=%d pos=%d local=%d official=%d level=%d\n",
                        c, local_pos, local_br_ctx, official_br_ctx, level);
            break;
        }
    }
    if (!br_ctx_mismatch) std::printf("  br contexts match for all level>2 coeffs\n");
}

}  // namespace

int main() {
    std::array<int16_t, kTxArea> huge_dc{};
    huge_dc[0] = -888;

    std::array<int16_t, kTxArea> dense_case = {
        -322, -64,   0,  -6,   0,  -2,   0,   0,
         -64, -14,  30,   0,   5,   0,   2,   0,
           0,  30,  14, -17,   0,  -4,   0,  -1,
          -7,   0, -17, -14,  11,   0,   2,   0,
           0,   5,   0,  11,  14,  -7,   0,  -1,
          -2,   0,  -3,   0,  -7, -14,   5,   0,
           0,   2,   0,   2,   0,   5,  14,  -3,
          -1,   0,  -1,   0,  -1,   0,  -3, -14
    };

    compare_case("huge_dc", huge_dc);
    compare_case("dense_case", dense_case);
    return 0;
}
