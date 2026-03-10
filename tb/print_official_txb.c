#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "av1/common/scan.h"
#include "av1/common/txb_common.h"
#include "av1/encoder/encodetxb.h"

static void dump_case(const char *label, const tran_low_t *qcoeff) {
  const TX_SIZE tx_size = TX_8X8;
  const TX_TYPE tx_type = DCT_DCT;
  const SCAN_ORDER *scan_order = get_scan(tx_size, tx_type);
  const int16_t *scan = scan_order->scan;
  uint8_t levels_buf[TX_PAD_2D];
  uint8_t *levels = set_levels(levels_buf, get_txb_high(tx_size));
  int8_t coeff_contexts[MAX_TX_SQUARE];
  int eob = 0;
  const int bhl = get_txb_bhl(tx_size);

  memset(levels_buf, 0, sizeof(levels_buf));
  for (int c = 0; c < 64; ++c) {
    if (qcoeff[scan[c]] != 0) eob = c + 1;
  }

  av1_txb_init_levels_c(qcoeff, get_txb_wide(tx_size), get_txb_high(tx_size), levels);
  memset(coeff_contexts, 0, sizeof(coeff_contexts));
  av1_get_nz_map_contexts_c(levels, scan, eob, tx_size, TX_CLASS_2D, coeff_contexts);

  printf("%s\n", label);
  printf("eob=%d scan:", eob);
  for (int c = 0; c < eob; ++c) printf(" %d", scan[c]);
  printf("\n");

  for (int c = eob - 1; c >= 0; --c) {
    const int pos = scan[c];
    const int level = abs((int)qcoeff[pos]);
    printf("  c=%d pos=%d level=%d ctx=%d", c, pos, level, coeff_contexts[pos]);
    if (level > 2) printf(" br_ctx=%d", get_br_ctx(levels, pos, bhl, TX_CLASS_2D));
    printf("\n");
  }
}

int main(void) {
  tran_low_t dc10[64] = { 0 };
  tran_low_t dc10_ac1_toprow[64] = { 0 };

  dc10[0] = 10;
  /* Local row-major physical coeff[1] maps to libaom's column-major index 8. */
  dc10_ac1_toprow[0] = 10;
  dc10_ac1_toprow[8] = 1;

  dump_case("dc10", dc10);
  dump_case("dc10+ac1(top row)", dc10_ac1_toprow);
  return 0;
}
