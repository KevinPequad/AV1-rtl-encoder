# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Match the reduced LAST-ref MV stack reach to AOM's `MVREF_ROW_COLS == 3` in both `tb/av1_bitstream_writer.h` and `rtl/av1_encoder_top.v`.
- Remove the extra far-row / far-column `+4` ref-MV weight that was drifting the first longer-motion NEWMV candidate on the `64x64` natural-motion guard.
- Restore exact longer-motion ownership on the repaired natural-motion guards:
  - `output/natural_motion64_x640_y360_7f_fixmvref64/` at `64x64`, `7` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
  - `output/natural_motion64_x640_y360_10f_progress70m/` at `64x64`, `10` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
- Measure the real longer-sequence runtime envelope:
  - the full `10`-frame `64x64` natural-motion guard is not hung; it completes at cycle `65669905`
  - use `+timeout=70000000` or higher for that guard on this machine

## Next Slice

1. Start the first fractional-pel translational inter slice on the reduced LAST-only path.
2. Mirror the writer's latent subpel MV payload support onto the RTL syntax path, then enable the smallest half-pel ownership checkpoint.
3. Reuse the repaired `7`-frame and `10`-frame exact guards after the first subpel step lands.

## Regression Gates

- `output/highdc_q1/` for strict large-DC coefficient ownership
- `data/ac_probe_16x16_1f.yuv` at `qindex=240` when that asset is available in the checkout
- `output/natural_focus_x640_y360_q128/` for strict natural `16x16` DC-only exactness
- `data/natural_repeat64_x640_y360_2f.yuv` at `qindex=128` for larger natural-content zero-motion inter exactness
- `data/natural_motion64_x640_y360_2f.yuv`, `data/natural_motion64_x640_y360_3f.yuv`, and `data/natural_motion32_x640_y360_3f.yuv` at `qindex=128` for reduced natural-motion inter exactness
- `output/natural_motion32_x640_y360_5f_fix1/`, `output/natural_motion64_x640_y360_5f_fix1/`, and `output/natural_motion64_x640_y360_6f_fix1/` as the shorter exact longer-motion guards
- `output/natural_motion64_x640_y360_7f_fixmvref64/` and `output/natural_motion64_x640_y360_10f_progress70m/` as the repaired exact longer-motion guards

## Local Notes

- `data/ac_probe_16x16_1f.yuv` is not present in this checkout, so the original exact-match `16x16` gate cannot be rerun locally yet.
- `data/tmp_probe_16x16_1f.yuv` is decode-clean but not byte-exact in this checkout; do not use it as the ownership gate.
- The first `7`-frame drift was not MV payload packing; the reduced ref-MV stack was reaching one row and one column farther than AOM's `MVREF_ROW_COLS == 3` search and was overweighting a later NEWMV candidate by `+4`.
- The full `10`-frame `64x64` natural-motion guard needs a higher timeout than the older `20`-minute wrapper. The bounded `+progress_every=5000000 +timeout=70000000` run is now the reference command when checking that guard.
