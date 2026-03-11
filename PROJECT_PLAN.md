# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Land the first fractional-pel syntax-only inter slice on the reduced LAST-only path without widening the predictor datapath yet:
  - `tb/av1_bitstream_writer.h`, `rtl/av1_encoder_top.v`, and `rtl/av1_bitstream.v` now emit `force_integer_mv=0`, `allow_high_precision_mv=1`, plus the real `mv_fr` and `mv_hp` symbols on reduced `NEWMV` components
  - `tb/test_rtl_bitstream.cpp` and `make bitstream-check WIDTH=16 HEIGHT=16` now lock the reduced inter header against that header order
- Fix the shared strict-decoder corruption on the first inter frame after enabling subpel syntax:
  - the first drift was not in the raw-byte mux; the common writer/RTL model was missing the `mv_hp` payload symbol after `mv_fr`
  - the remaining corruption was the missing `allow_high_precision_mv` frame-header bit after `force_integer_mv=0`
- Verify the syntax-only subpel slice on the current integer-MV motion guards:
  - `output/natural_motion64_x640_y360_2f_subpel2/` at `64x64`, `2` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
  - `output/natural_motion64_x640_y360_7f_subpel2/` at `64x64`, `7` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
  - `output/natural_motion64_x640_y360_10f_subpel2/` at `64x64`, `10` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
- Reconfirm the current longer-sequence runtime envelope after the header/subpel syntax move:
  - the full `10`-frame `64x64` natural-motion guard still completes at cycle `65670737`
  - keep using `+timeout=70000000` or higher for that guard on this machine

## Next Slice

1. Enable the first real non-zero fractional-pel translational checkpoint on the reduced LAST-only path.
2. Bring fractional predictor / ME behavior onto the smallest natural-motion guard without regressing the new syntax-only subpel checkpoints.
3. Reuse the `2f`, `7f`, and `10f` subpel exact guards after the first non-zero subpel datapath step lands.

## Regression Gates

- `output/highdc_q1/` for strict large-DC coefficient ownership
- `data/ac_probe_16x16_1f.yuv` at `qindex=240` when that asset is available in the checkout
- `output/natural_focus_x640_y360_q128/` for strict natural `16x16` DC-only exactness
- `data/natural_repeat64_x640_y360_2f.yuv` at `qindex=128` for larger natural-content zero-motion inter exactness
- `data/natural_motion64_x640_y360_2f.yuv`, `data/natural_motion64_x640_y360_3f.yuv`, and `data/natural_motion32_x640_y360_3f.yuv` at `qindex=128` for reduced natural-motion inter exactness
- `output/natural_motion64_x640_y360_2f_subpel2/` as the shortest exact syntax-only subpel guard
- `output/natural_motion32_x640_y360_5f_fix1/`, `output/natural_motion64_x640_y360_5f_fix1/`, and `output/natural_motion64_x640_y360_6f_fix1/` as the shorter exact longer-motion guards
- `output/natural_motion64_x640_y360_7f_fixmvref64/` and `output/natural_motion64_x640_y360_10f_progress70m/` as the repaired exact longer-motion guards
- `output/natural_motion64_x640_y360_7f_subpel2/` and `output/natural_motion64_x640_y360_10f_subpel2/` as the exact longer-motion syntax-only subpel guards

## Local Notes

- `data/ac_probe_16x16_1f.yuv` is not present in this checkout, so the original exact-match `16x16` gate cannot be rerun locally yet.
- `data/tmp_probe_16x16_1f.yuv` is decode-clean but not byte-exact in this checkout; do not use it as the ownership gate.
- The first strict decoder failure after enabling `force_integer_mv=0` was shared writer/RTL syntax, not ownership drift:
  - `mv_hp` was missing after `mv_fr` on reduced `NEWMV` components
  - `allow_high_precision_mv` was missing in the reduced inter frame header after `force_integer_mv=0`
- The full `10`-frame `64x64` natural-motion guard remains runtime-heavy after the subpel syntax step. The bounded `+progress_every=5000000 +timeout=70000000` run is still the reference command when checking that guard.
