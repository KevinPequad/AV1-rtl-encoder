#include "av1_bitstream_writer.h"

extern "C" {
#include "aom_dsp/bitwriter.h"
}

#include <cstdio>
#include <cstdint>
#include <vector>

struct EncodedBytes {
    std::vector<uint8_t> bytes;
};

static EncodedBytes encode_local_dc10() {
    AV1RangeCoder rc;
    rc.init();
    rc.encode_symbol(0, av1_txb_skip_cdf[0], 2);
    rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[0], 7);
    rc.encode_symbol(0, av1_eob_multi64_cdf[0], 7);
    rc.encode_symbol(2, av1_coeff_base_eob_cdf[0], 3);
    rc.encode_symbol(3, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(1, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(0, av1_dc_sign_cdf[0][0], 2);
    return {rc.finish()};
}

static EncodedBytes encode_local_dc10_ac1() {
    AV1RangeCoder rc;
    rc.init();
    rc.encode_symbol(0, av1_txb_skip_cdf[0], 2);
    rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[0], 7);
    rc.encode_symbol(1, av1_eob_multi64_cdf[0], 7);
    rc.encode_symbol(0, av1_coeff_base_eob_cdf[1], 3);
    rc.encode_symbol(3, av1_coeff_base_cdf[0], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(1, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(0, av1_dc_sign_cdf[0][0], 2);
    rc.encode_bit(0);
    return {rc.finish()};
}

static void encode_first_block_prefix_local(AV1RangeCoder& rc) {
    rc.encode_symbol(3, av1_partition_cdf[12], 10); // 64x64 -> split
    rc.encode_symbol(3, av1_partition_cdf[8], 10);  // 32x32 -> split
    rc.encode_symbol(3, av1_partition_cdf[4], 10);  // 16x16 -> split
    rc.encode_symbol(0, av1_partition_cdf[0], 4);   // 8x8 -> none
    rc.encode_symbol(0, av1_skip_cdf[0], 2);
    rc.encode_symbol(0, av1_kf_y_mode_cdf[0][0], 13);
    rc.encode_symbol(0, av1_uv_mode_cdf_cfl[0], 14);
}

static void encode_first_block_suffix_local(AV1RangeCoder& rc) {
    rc.encode_symbol(1, av1_txb_skip_cdf_4x4[7], 2);
    rc.encode_symbol(1, av1_txb_skip_cdf_4x4[7], 2);
}

static EncodedBytes encode_local_block_dc10() {
    AV1RangeCoder rc;
    rc.init();
    encode_first_block_prefix_local(rc);
    rc.encode_symbol(0, av1_txb_skip_cdf[0], 2);
    rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[0], 7);
    rc.encode_symbol(0, av1_eob_multi64_cdf[0], 7);
    rc.encode_symbol(2, av1_coeff_base_eob_cdf[0], 3);
    rc.encode_symbol(3, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(1, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(0, av1_dc_sign_cdf[0][0], 2);
    encode_first_block_suffix_local(rc);
    return {rc.finish()};
}

static EncodedBytes encode_local_block_dc10_ac1() {
    AV1RangeCoder rc;
    rc.init();
    encode_first_block_prefix_local(rc);
    rc.encode_symbol(0, av1_txb_skip_cdf[0], 2);
    rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[0], 7);
    rc.encode_symbol(1, av1_eob_multi64_cdf[0], 7);
    rc.encode_symbol(0, av1_coeff_base_eob_cdf[1], 3);
    rc.encode_symbol(3, av1_coeff_base_cdf[0], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(1, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(0, av1_dc_sign_cdf[0][0], 2);
    rc.encode_bit(0);
    encode_first_block_suffix_local(rc);
    return {rc.finish()};
}

static void encode_8x8_prefix_local(AV1RangeCoder& rc) {
    rc.encode_symbol(0, av1_partition_cdf[0], 4);
    rc.encode_symbol(0, av1_skip_cdf[0], 2);
    rc.encode_symbol(0, av1_kf_y_mode_cdf[0][0], 13);
    rc.encode_symbol(0, av1_uv_mode_cdf_cfl[0], 14);
}

static EncodedBytes encode_local_8x8_dc10() {
    AV1RangeCoder rc;
    rc.init();
    encode_8x8_prefix_local(rc);
    rc.encode_symbol(0, av1_txb_skip_cdf[0], 2);
    rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[0], 7);
    rc.encode_symbol(0, av1_eob_multi64_cdf[0], 7);
    rc.encode_symbol(2, av1_coeff_base_eob_cdf[0], 3);
    rc.encode_symbol(3, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(1, av1_coeff_br_cdf[0], 4);
    rc.encode_symbol(0, av1_dc_sign_cdf[0][0], 2);
    encode_first_block_suffix_local(rc);
    return {rc.finish()};
}

static EncodedBytes encode_local_8x8_dc10_ac1() {
    AV1RangeCoder rc;
    rc.init();
    encode_8x8_prefix_local(rc);
    rc.encode_symbol(0, av1_txb_skip_cdf[0], 2);
    rc.encode_symbol(1, av1_intra_tx_type_cdf_8x8[0], 7);
    rc.encode_symbol(1, av1_eob_multi64_cdf[0], 7);
    rc.encode_symbol(0, av1_coeff_base_eob_cdf[1], 3);
    rc.encode_symbol(3, av1_coeff_base_cdf[0], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(3, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(1, av1_coeff_br_cdf[1], 4);
    rc.encode_symbol(0, av1_dc_sign_cdf[0][0], 2);
    rc.encode_bit(0);
    encode_first_block_suffix_local(rc);
    return {rc.finish()};
}

static EncodedBytes encode_aom_dc10() {
    std::vector<uint8_t> out(1024);
    aom_writer w;
    aom_start_encode(&w, out.data());
    w.allow_update_cdf = 0;
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf[0])), 2);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_intra_tx_type_cdf_8x8[0])), 7);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_eob_multi64_cdf[0])), 7);
    aom_write_symbol(&w, 2, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_eob_cdf[0])), 3);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_dc_sign_cdf[0][0])), 2);
    aom_stop_encode(&w);
    out.resize(w.pos);
    return {out};
}

static EncodedBytes encode_aom_dc10_ac1() {
    std::vector<uint8_t> out(1024);
    aom_writer w;
    aom_start_encode(&w, out.data());
    w.allow_update_cdf = 0;
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf[0])), 2);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_intra_tx_type_cdf_8x8[0])), 7);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_eob_multi64_cdf[0])), 7);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_eob_cdf[1])), 3);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_cdf[0])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_dc_sign_cdf[0][0])), 2);
    aom_write_bit(&w, 0);
    aom_stop_encode(&w);
    out.resize(w.pos);
    return {out};
}

static void encode_first_block_prefix_aom(aom_writer* w) {
    aom_write_symbol(w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_partition_cdf[12])), 10);
    aom_write_symbol(w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_partition_cdf[8])), 10);
    aom_write_symbol(w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_partition_cdf[4])), 10);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_partition_cdf[0])), 4);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_skip_cdf[0])), 2);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_kf_y_mode_cdf[0][0])), 13);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_uv_mode_cdf_cfl[0])), 14);
}

static void encode_first_block_suffix_aom(aom_writer* w) {
    aom_write_symbol(w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf_4x4[7])), 2);
    aom_write_symbol(w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf_4x4[7])), 2);
}

static EncodedBytes encode_aom_block_dc10() {
    std::vector<uint8_t> out(1024);
    aom_writer w;
    aom_start_encode(&w, out.data());
    w.allow_update_cdf = 0;
    encode_first_block_prefix_aom(&w);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf[0])), 2);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_intra_tx_type_cdf_8x8[0])), 7);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_eob_multi64_cdf[0])), 7);
    aom_write_symbol(&w, 2, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_eob_cdf[0])), 3);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_dc_sign_cdf[0][0])), 2);
    encode_first_block_suffix_aom(&w);
    aom_stop_encode(&w);
    out.resize(w.pos);
    return {out};
}

static EncodedBytes encode_aom_block_dc10_ac1() {
    std::vector<uint8_t> out(1024);
    aom_writer w;
    aom_start_encode(&w, out.data());
    w.allow_update_cdf = 0;
    encode_first_block_prefix_aom(&w);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf[0])), 2);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_intra_tx_type_cdf_8x8[0])), 7);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_eob_multi64_cdf[0])), 7);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_eob_cdf[1])), 3);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_cdf[0])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_dc_sign_cdf[0][0])), 2);
    aom_write_bit(&w, 0);
    encode_first_block_suffix_aom(&w);
    aom_stop_encode(&w);
    out.resize(w.pos);
    return {out};
}

static void encode_8x8_prefix_aom(aom_writer* w) {
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_partition_cdf[0])), 4);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_skip_cdf[0])), 2);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_kf_y_mode_cdf[0][0])), 13);
    aom_write_symbol(w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_uv_mode_cdf_cfl[0])), 14);
}

static EncodedBytes encode_aom_8x8_dc10() {
    std::vector<uint8_t> out(1024);
    aom_writer w;
    aom_start_encode(&w, out.data());
    w.allow_update_cdf = 0;
    encode_8x8_prefix_aom(&w);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf[0])), 2);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_intra_tx_type_cdf_8x8[0])), 7);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_eob_multi64_cdf[0])), 7);
    aom_write_symbol(&w, 2, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_eob_cdf[0])), 3);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[0])), 4);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_dc_sign_cdf[0][0])), 2);
    encode_first_block_suffix_aom(&w);
    aom_stop_encode(&w);
    out.resize(w.pos);
    return {out};
}

static EncodedBytes encode_aom_8x8_dc10_ac1() {
    std::vector<uint8_t> out(1024);
    aom_writer w;
    aom_start_encode(&w, out.data());
    w.allow_update_cdf = 0;
    encode_8x8_prefix_aom(&w);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_txb_skip_cdf[0])), 2);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_intra_tx_type_cdf_8x8[0])), 7);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_eob_multi64_cdf[0])), 7);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_eob_cdf[1])), 3);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_base_cdf[0])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 3, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 1, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_coeff_br_cdf[1])), 4);
    aom_write_symbol(&w, 0, const_cast<aom_cdf_prob*>(reinterpret_cast<const aom_cdf_prob*>(av1_dc_sign_cdf[0][0])), 2);
    aom_write_bit(&w, 0);
    encode_first_block_suffix_aom(&w);
    aom_stop_encode(&w);
    out.resize(w.pos);
    return {out};
}

static void dump_case(const char* label, const EncodedBytes& a, const EncodedBytes& b) {
    printf("%s\n", label);
    printf("  local bytes: %zu\n", a.bytes.size());
    printf("  local data:");
    for (uint8_t v : a.bytes) printf(" %02x", v);
    printf("\n");
    printf("  aom   bytes: %zu\n", b.bytes.size());
    printf("  aom   data:");
    for (uint8_t v : b.bytes) printf(" %02x", v);
    printf("\n");
    const size_t n = a.bytes.size() > b.bytes.size() ? a.bytes.size() : b.bytes.size();
    for (size_t i = 0; i < n; ++i) {
        const int av = i < a.bytes.size() ? a.bytes[i] : -1;
        const int bv = i < b.bytes.size() ? b.bytes[i] : -1;
        if (av != bv) {
            printf("  first diff @%zu: local=%d aom=%d\n", i, av, bv);
            return;
        }
    }
    printf("  byte-identical\n");
}

int main() {
    dump_case("dc10", encode_local_dc10(), encode_aom_dc10());
    dump_case("dc10+ac1", encode_local_dc10_ac1(), encode_aom_dc10_ac1());
    dump_case("block dc10", encode_local_block_dc10(), encode_aom_block_dc10());
    dump_case("block dc10+ac1", encode_local_block_dc10_ac1(), encode_aom_block_dc10_ac1());
    dump_case("8x8 dc10", encode_local_8x8_dc10(), encode_aom_8x8_dc10());
    dump_case("8x8 dc10+ac1", encode_local_8x8_dc10_ac1(), encode_aom_8x8_dc10_ac1());
    return 0;
}
