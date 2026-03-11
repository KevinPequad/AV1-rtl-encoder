# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Restore exact larger natural-content zero-motion inter ownership on the raw RTL byte path.
- Route coefficient-bearing zero-motion inter blocks through the real generic coefficient syntax path and the correct inter `DCT_DCT` `tx_type` CDF in `rtl/av1_encoder_top.v`.
- Verify the first natural-content `64x64` 2-frame repeated-frame checkpoint:
  - input: `data/natural_repeat64_x640_y360_2f.yuv`
  - setting: `qindex=128`
  - result: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`

## Next Slice

1. Implement reduced raw-path `NEARESTMV` / `NEWMV` mode signaling in `rtl/av1_encoder_top.v` for the current single-reference inter subset.
2. Add the matching MV payload syntax once the mode symbols are exact.
3. Extend the verified multi-frame regression set beyond repeated-frame zero-motion content.

## Regression Gates

- `output/highdc_q1/` for strict large-DC coefficient ownership
- `data/ac_probe_16x16_1f.yuv` at `qindex=240` when that asset is available in the checkout
- `output/natural_focus_x640_y360_q128/` for strict natural `16x16` DC-only exactness
- `data/natural_repeat64_x640_y360_2f.yuv` at `qindex=128` for larger natural-content zero-motion inter exactness

## Local Notes

- `data/ac_probe_16x16_1f.yuv` is not present in this checkout, so the original exact-match `16x16` gate cannot be rerun locally yet.
- `data/tmp_probe_16x16_1f.yuv` is decode-clean but not byte-exact in this checkout; do not use it as the ownership gate.
