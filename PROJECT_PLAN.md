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

## P7 Boundary Checkpoint

- `P7_REFERENCE_BOUNDARY.md` records the current LAST-only inter contract, the deferred compound/reference families, and the exact P7 evidence.
- On canonical `main` at `8994490d209a745fb157e999af616b49de2c6ce1` (already pushed to `origin/main`), the single-threaded current matrix passed 21/21 gates: `make bitstream-check WIDTH=16 HEIGHT=16`, `make gop-lifecycle-syntax-check`, `make natural64-ip-mode-context-syntax-check` as the syntax/byte-ownership gate, and `make natural64-ip-fractional-syntax-check` as the q3 public-decoder proof alongside the existing 32x32/64x64 inter gates.
- This is a staging checkpoint toward full RTL AV1 completion, not final scope closure; compound / multi-ref tools remain deferred for the reduced low-delay target.

## Next Slice

1. Widen the now-passing 32x32 zero-MV natural-ish IP residual proof toward non-zero/fractional-MV inter natural clips.
2. Keep `nonzero-chroma-syntax-check`, `nonzero-chroma16-syntax-check`, `natural32-chroma-syntax-check`, and `natural32-ip-syntax-check` public-decoder gates passing while widening the fixture.
3. Continue debugging the unconstrained 32x32 non-zero-MV natural IP mismatch as the next motion-specific blocker without regressing the zero-MV P-frame residual gate.

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

Latest progress: 32x32 natural-ish all-key luma/chroma is now RTL-owned and public-decoder verified with `make THREADS=16 BUILD_JOBS=16 natural32-chroma-syntax-check`. The 8x8 and 16x16 non-zero chroma gates remain passing as isolated/widened chroma syntax guards.


## Chud PC 2 non-zero chroma syntax checkpoints

Implemented public-decoder gates for both isolated and multi-block non-zero Cb/Cr TX_4X4 syntax:

- `nonzero-chroma-syntax-check` keeps the constrained 8x8 one-block proof for Cb/Cr TX_4X4 `txb_skip`, EOB, base, BR/sign syntax.
- `nonzero-chroma16-syntax-check` widens that proof to a 16x16 all-key non-flat chroma frame with multiple chroma transform blocks.
- `natural32-chroma-syntax-check` widens again to a 32x32 deterministic natural-ish gradient probe with non-flat luma and chroma.
- `natural32-ip-syntax-check` adds a two-frame 32x32 natural-ish zero-MV IP residual proof using RTL-owned P-frame bytes.
- The 16x16 widening added decoder-matching intra chroma DC prediction from reconstructed current-frame chroma neighbors and mirrored Cb/Cr entropy contexts in RTL.
- The 32x32 widening fixed explicit Cb/Cr txb context selection in RTL so `chr_syntax_plane` same-cycle updates cannot swap contexts between planes.

Verification gates:

```bash
cd tb
make THREADS=16 BUILD_JOBS=16 nonzero-chroma-syntax-check
make THREADS=16 BUILD_JOBS=16 nonzero-chroma16-syntax-check
make THREADS=16 BUILD_JOBS=16 natural32-chroma-syntax-check
make THREADS=16 BUILD_JOBS=16 natural32-ip-syntax-check
```

All four gates verify RTL raw OBU equality plus FFmpeg/aomdec decode-vs-`recon.yuv` parity.
