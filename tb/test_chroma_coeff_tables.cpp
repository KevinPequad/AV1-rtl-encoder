#include <cstdint>
#include <iostream>

#include "av1_tx4x4_chroma_tables.h"

int main() {
    bool ok = true;
    auto check = [&](const char* name, uint16_t got, uint16_t exp) {
        if (got != exp) {
            std::cerr << "[FAIL] " << name << " got " << got << " expected " << exp << "\n";
            ok = false;
        }
    };

    // Values from AOM's token_cdfs.h through AOM_CDF* ICDF conversion.
    check("eob_multi16_chroma_q3_sym0", av1_eob_multi16_chroma_cdf_qctx[3][0], 13193);
    check("eob_multi16_chroma_q3_sym3", av1_eob_multi16_chroma_cdf_qctx[3][3], 3059);
    check("eob_extra_chroma4_q3_ctx0", av1_eob_extra_chroma4_cdf_qctx[3][0][0], 11352);
    check("base_eob_chroma4_q3_ctx0_sym1", av1_coeff_base_eob_chroma4_cdf_qctx[3][0][1], 1184);
    check("base_chroma4_q3_ctx0_sym1", av1_coeff_base_chroma4_cdf_qctx[3][0][1], 10666);
    check("br_chroma4_q3_ctx0_sym2", av1_coeff_br_chroma4_cdf_qctx[3][0][2], 2380);

    if (ok) std::cout << "[PASS] chroma TX_4X4 coefficient tables\n";
    return ok ? 0 : 1;
}
