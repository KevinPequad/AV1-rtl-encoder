# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Fix the ME bottom-right zero-skip bug in `rtl/av1_me.v` so longer clips no longer spin in `TS_WAIT_ME` when zero MV is also the final legal raster-search candidate.
- Extend the longer-motion exactness guards beyond the old `3`-frame ceiling:
  - `output/natural_motion32_x640_y360_5f_fix1/` at `32x32`, `5` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `output/natural_motion64_x640_y360_5f_fix1/` at `64x64`, `5` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `output/natural_motion64_x640_y360_6f_fix1/` at `64x64`, `6` frames, `qindex=128`: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
- Narrow the first remaining longer-sequence drift:
  - `output/natural_motion64_x640_y360_7f_probe/` first differs at byte `449`
  - the first six frames of that `64x64` natural-motion sequence are now exact

## Next Slice

1. Debug the first `64x64` natural-motion ownership drift on `output/natural_motion64_x640_y360_7f_probe/`.
2. Identify the first frame-`0006` syntax mismatch with the existing writer / entropy trace hooks.
3. Restore byte-exact OBU/IVF matching on the `7`-frame repro, then rerun the `10`-frame guard.

## Regression Gates

- `output/highdc_q1/` for strict large-DC coefficient ownership
- `data/ac_probe_16x16_1f.yuv` at `qindex=240` when that asset is available in the checkout
- `output/natural_focus_x640_y360_q128/` for strict natural `16x16` DC-only exactness
- `data/natural_repeat64_x640_y360_2f.yuv` at `qindex=128` for larger natural-content zero-motion inter exactness
- `data/natural_motion64_x640_y360_2f.yuv`, `data/natural_motion64_x640_y360_3f.yuv`, and `data/natural_motion32_x640_y360_3f.yuv` at `qindex=128` for reduced natural-motion inter exactness
- `output/natural_motion32_x640_y360_5f_fix1/`, `output/natural_motion64_x640_y360_5f_fix1/`, and `output/natural_motion64_x640_y360_6f_fix1/` as the current exact longer-motion guards
- `output/natural_motion64_x640_y360_7f_probe/` as the first failing longer-motion repro

## Local Notes

- `data/ac_probe_16x16_1f.yuv` is not present in this checkout, so the original exact-match `16x16` gate cannot be rerun locally yet.
- `data/tmp_probe_16x16_1f.yuv` is decode-clean but not byte-exact in this checkout; do not use it as the ownership gate.
- The old longer-motion runtime wall was a real ME scan bug on bottom-right blocks, not just simulation cost. The `5`-frame and `6`-frame guards now complete and match exactly after the fix.
