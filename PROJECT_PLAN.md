# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Implement reduced raw-path `NEARESTMV` / `NEWMV` mode signaling plus the matching integer MV payload syntax in `rtl/av1_encoder_top.v` for the current single-reference LAST-ref subset.
- Derive a reduced neighboring ref-MV stack on the RTL path and persist per-block integer MVs so inter blocks can emit real `refmv`, `drl`, `mv_joint`, sign, class, class0, and class-bit syntax instead of the old zero-motion-only scaffold.
- Verify the first natural-motion ownership checkpoints:
  - `data/natural_motion64_x640_y360_2f.yuv` at `64x64`, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `data/natural_motion64_x640_y360_3f.yuv` at `64x64`, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `data/natural_motion32_x640_y360_3f.yuv` at `32x32`, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`

## Next Slice

1. Establish a practical longer multi-frame motion regression guard beyond the current `3`-frame exact cases without exploding runtime.
2. If the current exhaustive-ME runs remain too expensive, add a narrower reproducible motion guard or debug knob that keeps the verification signal while shortening turnaround.
3. Re-verify software-owned vs RTL-owned OBU/IVF exactness plus decoded-vs-`recon.yuv` matching on that longer-motion guard.

## Regression Gates

- `output/highdc_q1/` for strict large-DC coefficient ownership
- `data/ac_probe_16x16_1f.yuv` at `qindex=240` when that asset is available in the checkout
- `output/natural_focus_x640_y360_q128/` for strict natural `16x16` DC-only exactness
- `data/natural_repeat64_x640_y360_2f.yuv` at `qindex=128` for larger natural-content zero-motion inter exactness
- `data/natural_motion64_x640_y360_2f.yuv`, `data/natural_motion64_x640_y360_3f.yuv`, and `data/natural_motion32_x640_y360_3f.yuv` at `qindex=128` for reduced natural-motion inter exactness

## Local Notes

- `data/ac_probe_16x16_1f.yuv` is not present in this checkout, so the original exact-match `16x16` gate cannot be rerun locally yet.
- `data/tmp_probe_16x16_1f.yuv` is decode-clean but not byte-exact in this checkout; do not use it as the ownership gate.
- The current `64x64` `5`-frame and `10`-frame natural-motion runs timed out under the present local runtime budget, so the next slice should either find a practical longer guard or reduce the motion-run cost.
