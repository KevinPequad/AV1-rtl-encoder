# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Fix the Chud PC 2 generated `16x16` smoke ownership blocker:
  - generated non-flat all-key and 2-frame IP smoke cases are now byte-exact between software-owned and RTL-owned raw OBU/IVF artifacts
  - both `ffmpeg`/libdav1d and `aomdec` decode the RTL IVF back to `recon.yuv`
  - the fix keeps the bootstrap frame on the RTL-owned video keyframe header path, aligns the software writer with static-CDF mode, and uses the actual RTL append address for frame OBU size back-patching
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

1. Expand the new one-block non-zero Cb/Cr TX_4X4 syntax proof to multi-block 16x16+ non-flat chroma by adding decoder-matching chroma intra prediction from reconstructed chroma neighbors.
2. Keep the current `nonzero-chroma-syntax-check` public-decoder gate passing while widening the fixture.
3. Continue widening the real non-zero fractional-pel translational checkpoints on the reduced LAST-only path without regressing the syntax-only subpel guards.

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


## Chud PC 2 smoke blocker

On Chud PC 2, Verilator 5.020 required two top-level compile fixes in `rtl/av1_encoder_top.v`:

- avoid slicing a function-call result directly; route those coefficient symbols through helper functions instead
- avoid mixed blocking/nonblocking assignments on `intra_cand_sad`; compute the SAD into `intra_cand_sad_next` and register it separately

Post-fix quick checks pass for `entropy-check`, `bitstream-check WIDTH=16 HEIGHT=16`, and `inv-xform-check` with `THREADS=16 BUILD_JOBS=16`. The generated Chud PC 2 `16x16` all-key and 2-frame IP smoke cases now also pass the strict ownership/decode gate: `encoded.obu == encoded_rtl_raw.obu`, `encoded.ivf == encoded_rtl.ivf`, and both FFmpeg/libdav1d and `aomdec` decoded output match `recon.yuv`. Resume the first real non-zero fractional-pel datapath checkpoint next.


## Chud PC 2 chroma residual top-level slice

Completed in the current working tree:
- top-level `av1_chroma_residual` instantiation for Cb/Cr 4x4 blocks
- captured `chr_cb_qcoeff[]`, `chr_cr_qcoeff[]`, `chr_cb_has_coeff`, and `chr_cr_has_coeff` for oracle/syntax integration
- `top-chroma-integration-check` static regression guard
- fixed the inter chroma wait hang by latching raw fetch and chroma predictor completion separately

Verified commands:
- `python3 tb/test_top_chroma_integration.py`
- `make THREADS=16 BUILD_JOBS=16 chroma-residual-check`
- `make THREADS=16 BUILD_JOBS=16 chroma-coeff-table-check`
- `make THREADS=16 BUILD_JOBS=16 WIDTH=16 HEIGHT=16 all`
- `THREADS=16 BUILD_JOBS=16 bash /tmp/av1_chudpc2_smoke.sh`

Latest progress: constrained one-block non-zero Cb/Cr TX_4X4 syntax is now RTL-owned and public-decoder verified with `make THREADS=16 BUILD_JOBS=16 nonzero-chroma-syntax-check`. Remaining blocker for wider proof: multi-block non-flat chroma needs decoder-matching chroma intra-neighbor prediction before the 16x16+ chroma probe can pass recon parity.


## Chud PC 2 non-zero chroma syntax checkpoint

Implemented a constrained one-block dynamic proof for non-zero Cb/Cr TX_4X4 syntax:

- C++ oracle and RTL both emit Cb/Cr `txb_skip`, EOB, base, BR/sign syntax from captured chroma qcoeffs.
- Fixed TX_4X4 EOB symbol count, removed invalid chroma-only luma tx_type insertion, and added separate oracle Cb/Cr entropy contexts.
- Adjusted chroma residual inverse scaling so public decoder reconstruction matches RTL `recon.yuv` for TX_4X4 DC.

Verification gate:

```bash
cd tb
make THREADS=16 BUILD_JOBS=16 nonzero-chroma-syntax-check
```

The gate uses an 8x8 all-key non-flat chroma probe and verifies RTL raw OBU equality plus FFmpeg/aomdec decode-vs-recon parity. The next expansion is 16x16+ non-flat chroma with decoder-matching chroma intra prediction from reconstructed chroma neighbors.
