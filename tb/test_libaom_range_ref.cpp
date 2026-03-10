#include "av1_bitstream_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

extern "C" {
#include "aom_dsp/bitwriter.h"
}

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

static void write_golomb_local(AV1RangeCoder& rc, aom_writer& w, int level) {
    const int x = level + 1;
    int length = 0;
    int tmp = x;
    while (tmp > 0) {
        ++length;
        tmp >>= 1;
    }
    for (int i = 0; i < length - 1; ++i) {
        rc.encode_bit(0);
        aom_write_bit(&w, 0);
    }
    for (int i = length - 1; i >= 0; --i) {
        const int bit = (x >> i) & 1;
        rc.encode_bit(bit);
        aom_write_bit(&w, bit);
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

int main() {
    AV1RangeCoder rc;
    rc.init();

    uint8_t out_buf[1024] = {0};
    aom_writer w;
    w.allow_update_cdf = 1;
    aom_start_encode(&w, out_buf);

    std::vector<uint16_t> txb_skip(std::begin(av1_txb_skip_cdf[0]),
                                   std::begin(av1_txb_skip_cdf[0]) + 3);
    std::vector<uint16_t> tx_type(std::begin(av1_intra_tx_type_cdf_8x8[1]),
                                  std::begin(av1_intra_tx_type_cdf_8x8[1]) + 8);
    std::vector<uint16_t> eob_multi(std::begin(av1_eob_multi64_cdf[0]),
                                    std::begin(av1_eob_multi64_cdf[0]) + 8);
    std::vector<uint16_t> coeff_base_eob(std::begin(av1_coeff_base_eob_cdf[0]),
                                         std::begin(av1_coeff_base_eob_cdf[0]) + 4);
    std::vector<uint16_t> coeff_br(std::begin(av1_coeff_br_cdf[0]),
                                   std::begin(av1_coeff_br_cdf[0]) + 5);
    std::vector<uint16_t> dc_sign(std::begin(av1_dc_sign_cdf[0][0]),
                                  std::begin(av1_dc_sign_cdf[0][0]) + 3);

    auto encode_symbol_both = [&](int sym, std::vector<uint16_t>& cdf, int nsyms) {
        rc.encode_symbol(sym, cdf.data(), nsyms);
        aom_write_symbol(&w, sym, reinterpret_cast<aom_cdf_prob*>(cdf.data()), nsyms);
        update_cdf_ref(cdf.data(), sym, nsyms);
    };

    // Exact failing sequence for the first 8x8 block on crop_560_0_16x16.yuv.
    encode_symbol_both(0, txb_skip, 2);
    encode_symbol_both(1, tx_type, 7);
    encode_symbol_both(0, eob_multi, 7);
    encode_symbol_both(2, coeff_base_eob, 3);
    encode_symbol_both(3, coeff_br, 4);
    encode_symbol_both(3, coeff_br, 4);
    encode_symbol_both(3, coeff_br, 4);
    encode_symbol_both(3, coeff_br, 4);
    encode_symbol_both(1, dc_sign, 2);
    write_golomb_local(rc, w, 888 - 12 - 1 - 2);

    const auto rc_bytes = rc.finish();
    const int nb_bits = aom_stop_encode(&w);
    (void)nb_bits;
    const std::vector<uint8_t> ref_bytes(out_buf, out_buf + w.pos);

    print_bytes("local", rc_bytes);
    print_bytes("libaom", ref_bytes);

    if (rc_bytes != ref_bytes) {
        std::cerr << "mismatch\n";
        return 1;
    }

    std::cout << "match\n";
    return 0;
}
