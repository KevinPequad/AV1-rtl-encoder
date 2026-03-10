#include <stdio.h>

#include "av1/common/token_cdfs.h"

int main(void) {
    const int qctx = 3;      // qindex > 120
    const int txs_ctx = 1;   // TX_8X8
    const int plane = 0;     // luma

    printf("eob_multi64 plane0: %u %u %u %u %u %u 0\n",
           av1_default_eob_multi64_cdfs[qctx][plane][0][0],
           av1_default_eob_multi64_cdfs[qctx][plane][0][1],
           av1_default_eob_multi64_cdfs[qctx][plane][0][2],
           av1_default_eob_multi64_cdfs[qctx][plane][0][3],
           av1_default_eob_multi64_cdfs[qctx][plane][0][4],
           av1_default_eob_multi64_cdfs[qctx][plane][0][5]);

    printf("coeff_base_eob ctx0: %u %u 0\n",
           av1_default_coeff_base_eob_multi_cdfs[qctx][txs_ctx][plane][0][0],
           av1_default_coeff_base_eob_multi_cdfs[qctx][txs_ctx][plane][0][1]);
    printf("coeff_base_eob ctx1: %u %u 0\n",
           av1_default_coeff_base_eob_multi_cdfs[qctx][txs_ctx][plane][1][0],
           av1_default_coeff_base_eob_multi_cdfs[qctx][txs_ctx][plane][1][1]);

  printf("coeff_base ctx0: %u %u %u 0\n",
           av1_default_coeff_base_multi_cdfs[qctx][txs_ctx][plane][0][0],
           av1_default_coeff_base_multi_cdfs[qctx][txs_ctx][plane][0][1],
           av1_default_coeff_base_multi_cdfs[qctx][txs_ctx][plane][0][2]);
  for (int ctx = 0; ctx < 42; ++ctx) {
    printf("coeff_base_full[%d] = {%u,%u,%u,0}\n", ctx,
           av1_default_coeff_base_multi_cdfs[qctx][txs_ctx][plane][ctx][0],
           av1_default_coeff_base_multi_cdfs[qctx][txs_ctx][plane][ctx][1],
           av1_default_coeff_base_multi_cdfs[qctx][txs_ctx][plane][ctx][2]);
  }

  printf("coeff_br ctx0: %u %u %u 0\n",
           av1_default_coeff_lps_multi_cdfs[qctx][txs_ctx][plane][0][0],
           av1_default_coeff_lps_multi_cdfs[qctx][txs_ctx][plane][0][1],
           av1_default_coeff_lps_multi_cdfs[qctx][txs_ctx][plane][0][2]);
    printf("coeff_br ctx7: %u %u %u 0\n",
           av1_default_coeff_lps_multi_cdfs[qctx][txs_ctx][plane][7][0],
           av1_default_coeff_lps_multi_cdfs[qctx][txs_ctx][plane][7][1],
           av1_default_coeff_lps_multi_cdfs[qctx][txs_ctx][plane][7][2]);

    return 0;
}
