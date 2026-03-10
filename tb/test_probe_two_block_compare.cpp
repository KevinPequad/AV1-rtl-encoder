#define private public
#include "av1_bitstream_writer.h"
#undef private

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "aom_dsp/bitwriter.h"
}

namespace {

struct CacheKey {
    const uint16_t* ptr;
    int nsyms;

    bool operator==(const CacheKey& other) const {
        return ptr == other.ptr && nsyms == other.nsyms;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const {
        return (reinterpret_cast<size_t>(key.ptr) >> 3) ^ static_cast<size_t>(key.nsyms * 131);
    }
};

static void update_cdf_ref(uint16_t* cdf, int val, int nsymbs) {
    const int count = cdf[nsymbs];
    const int rate = 4 + (count >> 4) + (nsymbs > 3);
    for (int i = 0; i < nsymbs - 1; ++i) {
        if (i < val)
            cdf[i] = static_cast<uint16_t>(cdf[i] + ((32768 - cdf[i]) >> rate));
        else
            cdf[i] = static_cast<uint16_t>(cdf[i] - (cdf[i] >> rate));
    }
    if (count < 32) cdf[nsymbs] = static_cast<uint16_t>(count + 1);
}

struct DualCoder {
    AV1RangeCoder local;
    aom_writer ref;
    uint8_t ref_buf[4096]{};
    std::unordered_map<CacheKey, std::vector<uint16_t>, CacheKeyHash> local_cdfs;
    std::unordered_map<CacheKey, std::vector<uint16_t>, CacheKeyHash> ref_cdfs;

    void init() {
        local.init();
        std::memset(ref_buf, 0, sizeof(ref_buf));
        aom_start_encode(&ref, ref_buf);
        ref.allow_update_cdf = 1;
        local_cdfs.clear();
        ref_cdfs.clear();
    }

    uint16_t* mutable_local(const uint16_t* cdf, int nsyms) {
        CacheKey key{cdf, nsyms};
        auto& slot = local_cdfs[key];
        if (slot.empty()) slot.assign(cdf, cdf + nsyms + 1);
        return slot.data();
    }

    uint16_t* mutable_ref(const uint16_t* cdf, int nsyms) {
        CacheKey key{cdf, nsyms};
        auto& slot = ref_cdfs[key];
        if (slot.empty()) slot.assign(cdf, cdf + nsyms + 1);
        return slot.data();
    }

    void symbol(int sym, const uint16_t* cdf, int nsyms) {
        uint16_t* lcdf = mutable_local(cdf, nsyms);
        uint16_t* rcdf = mutable_ref(cdf, nsyms);
        local.encode_symbol(sym, lcdf, nsyms);
        aom_write_symbol(&ref, sym, reinterpret_cast<aom_cdf_prob*>(rcdf), nsyms);
        update_cdf_ref(lcdf, sym, nsyms);
    }

    void symbol_no_update(int sym, const uint16_t* cdf, int nsyms) {
        local.encode_symbol(sym, cdf, nsyms);
        aom_write_cdf(&ref, sym, reinterpret_cast<const aom_cdf_prob*>(cdf), nsyms);
    }

    void bit(int b) {
        local.encode_bit(b);
        aom_write_bit(&ref, b);
    }

    std::vector<uint8_t> finish_local() {
        return local.finish();
    }

    std::vector<uint8_t> finish_ref() {
        aom_stop_encode(&ref);
        return std::vector<uint8_t>(ref_buf, ref_buf + ref.pos);
    }
};

static void encode_coeffs_dual(AV1BitstreamWriter& writer, DualCoder& dual, const int16_t* qcoeff,
                               int plane, int dc_sign_ctx, int intra_mode) {
    const int eob = writer.compute_eob(qcoeff);
    dual.symbol(eob == 0 ? 1 : 0, av1_txb_skip_cdf[0], 2);
    if (eob == 0) return;

    dual.symbol(1, av1_intra_tx_type_cdf_8x8[intra_mode], 7);

    int eob_extra = 0;
    const int eob_pt = get_eob_pos_token(eob, &eob_extra);
    dual.symbol(eob_pt - 1, av1_eob_multi64_cdf[plane], 7);
    const int eob_ob = eob_offset_bits[eob_pt];
    if (eob_ob > 0) {
        int eob_ctx = eob_pt - 3;
        int eob_shift = eob_ob - 1;
        dual.symbol((eob_extra >> eob_shift) & 1, av1_eob_extra_cdf[plane][eob_ctx], 2);
        for (int i = 1; i < eob_ob; ++i) {
            eob_shift = eob_ob - 1 - i;
            dual.bit((eob_extra >> eob_shift) & 1);
        }
    }

    uint8_t levels_buf[12 * 14 + 16]{};
    uint8_t* levels = levels_buf + 2 * 12;
    writer.init_levels_buf(qcoeff, 8, 8, levels);

    int8_t coeff_contexts[64]{};
    for (int c = 0; c < eob; ++c) {
        int pos = default_scan_8x8[c];
        if (c == eob - 1) {
            if (c == 0) coeff_contexts[pos] = 0;
            else if (c <= (8 * 8) / 8) coeff_contexts[pos] = 1;
            else if (c <= (8 * 8) / 4) coeff_contexts[pos] = 2;
            else coeff_contexts[pos] = 3;
        } else {
            coeff_contexts[pos] = static_cast<int8_t>(get_nz_map_ctx(levels, pos, 3));
        }
    }

    for (int c = eob - 1; c >= 0; --c) {
        int pos = default_scan_8x8[c];
        int level = std::abs(static_cast<int>(qcoeff[pos]));
        int coeff_ctx = coeff_contexts[pos];
        if (c == eob - 1) {
            dual.symbol(std::min(level, 3) - 1, av1_coeff_base_eob_cdf[std::min(coeff_ctx, 3)], 3);
        } else {
            dual.symbol(std::min(level, 3), av1_coeff_base_cdf[std::min(coeff_ctx, 41)], 4);
        }
        if (level > 2) {
            int base_range = level - 1 - 2;
            int br_ctx = std::min(get_br_ctx_2d(levels, pos, 3), 20);
            for (int idx = 0; idx < 12; idx += 3) {
                int k = std::min(base_range - idx, 3);
                dual.symbol(k, av1_coeff_br_cdf[br_ctx], 4);
                if (k < 3) break;
            }
        }
    }

    for (int c = 0; c < eob; ++c) {
        int pos = default_scan_8x8[c];
        int v = static_cast<int>(qcoeff[pos]);
        int level = std::abs(v);
        if (!level) continue;
        int sign = v < 0 ? 1 : 0;
        if (c == 0) {
            dual.symbol(sign, av1_dc_sign_cdf[plane][dc_sign_ctx], 2);
        } else {
            dual.bit(sign);
        }
        if (level > 14) {
            int remainder = level - 14 - 1;
            int x = remainder + 1;
            int length = 0;
            for (int tmp = x; tmp > 0; tmp >>= 1) ++length;
            for (int i = 0; i < length - 1; ++i) dual.bit(0);
            for (int i = length - 1; i >= 0; --i) dual.bit((x >> i) & 1);
        }
    }
}

static void print_bytes(const char* label, const std::vector<uint8_t>& bytes) {
    std::cout << label << " (" << bytes.size() << " bytes):";
    for (uint8_t b : bytes) {
        std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(b);
    }
    std::cout << std::dec << "\n";
}

}  // namespace

int main() {
    AV1BitstreamWriter writer(16, 16, 240);
    writer.init_contexts();

    AV1BitstreamWriter::BlockInfo blk0{};
    blk0.pred_mode = 1;
    blk0.qcoeff[0] = -1;
    blk0.qcoeff[1] = -1;

    AV1BitstreamWriter::BlockInfo blk1{};
    blk1.pred_mode = 7;
    blk1.qcoeff[0] = 2;

    DualCoder dual;
    dual.init();

    // 16x16 split
    dual.symbol(3, av1_partition_cdf[writer.get_partition_ctx(0, 0, 1)], 10);

    // Block 0
    dual.symbol(0, av1_partition_cdf[writer.get_partition_ctx(0, 0, 0)], 4);
    dual.symbol(0, av1_skip_cdf[writer.get_skip_ctx(0, 0)], 2);
    int above_ctx = 0, left_ctx = 0;
    writer.get_kf_y_mode_ctx(0, 0, above_ctx, left_ctx);
    dual.symbol(blk0.pred_mode, av1_kf_y_mode_cdf[above_ctx][left_ctx], 13);
    dual.symbol(3, av1_angle_delta_cdf[blk0.pred_mode - 1], 7);
    dual.symbol(0, av1_uv_mode_cdf_cfl[blk0.pred_mode], 14);
    encode_coeffs_dual(writer, dual, blk0.qcoeff, 0, writer.get_dc_sign_ctx(0, 0, 2), blk0.pred_mode);
    dual.symbol(1, av1_txb_skip_cdf_4x4[7], 2);
    dual.symbol(1, av1_txb_skip_cdf_4x4[7], 2);
    writer.update_block_ctx(0, 0, 2, 0, blk0.pred_mode, 1, false, 0xFF, 0, 0, 0);
    writer.update_partition_ctx(0, 0, 3);

    // Block 1
    dual.symbol(0, av1_partition_cdf[writer.get_partition_ctx(8, 0, 0)], 4);
    dual.symbol(0, av1_skip_cdf[writer.get_skip_ctx(0, 2)], 2);
    writer.get_kf_y_mode_ctx(0, 2, above_ctx, left_ctx);
    dual.symbol(blk1.pred_mode, av1_kf_y_mode_cdf[above_ctx][left_ctx], 13);
    dual.symbol(3, av1_angle_delta_cdf[blk1.pred_mode - 1], 7);
    dual.symbol(0, av1_uv_mode_cdf_cfl[blk1.pred_mode], 14);
    encode_coeffs_dual(writer, dual, blk1.qcoeff, 0, writer.get_dc_sign_ctx(0, 2, 2), blk1.pred_mode);
    dual.symbol(1, av1_txb_skip_cdf_4x4[7], 2);
    dual.symbol(1, av1_txb_skip_cdf_4x4[7], 2);

    const auto local_bytes = dual.finish_local();
    const auto ref_bytes = dual.finish_ref();
    print_bytes("local", local_bytes);
    print_bytes("libaom", ref_bytes);

    if (local_bytes != ref_bytes) {
        std::cerr << "mismatch\n";
        return 1;
    }

    std::cout << "match\n";
    return 0;
}
