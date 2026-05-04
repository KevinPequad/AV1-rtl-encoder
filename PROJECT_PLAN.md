# AV1 RTL Encoder Project Plan

## Current Slice

Completed:
- Landed and post-merge validated the controlled 32x32 natural-ish inter widening at commit `91e403a030b951be45b4954e5dfe433e8418272c` (`Add natural32 fractional NEWMV proof`):
  - `natural32-ip-syntax-check` covers the repeated-frame / zero-MV 32x32 reduced LAST inter residual path.
  - `natural32-ip-newmv-syntax-check` covers a shifted two-frame 32x32 fixture with an isolated non-zero-MV `NEWMV` inter block.
  - `natural32-ip-fractional-syntax-check` covers a half-pel synthesized second frame, requires exactly two small `NEWMV` blocks under `+me_newmv_limit=2`, and requires at least one fractional q3 MV.
  - All three gates compare concatenated RTL raw OBU bytes to the software oracle, then check FFmpeg/libdav1d and `aomdec` decode of RTL IVF against `recon.yuv`.
- Preserved the current chroma public-decoder gates in the same post-merge command:
  - `natural32-chroma-syntax-check`
  - `nonzero-chroma-syntax-check`
  - `nonzero-chroma16-syntax-check`
- Validation ran on Chud PC 2 (`chudpc2-MS-7C91`, `nproc=16`, Verilator 5.020) with:

```bash
cd /home/chudpc2/code/AV1-rtl-encoder/.worktrees/t_13da93e2/tb
make THREADS=16 BUILD_JOBS=16 natural32-ip-fractional-syntax-check natural32-ip-newmv-syntax-check natural32-ip-syntax-check natural32-chroma-syntax-check nonzero-chroma-syntax-check nonzero-chroma16-syntax-check
```

Scope of the claim:
- This is a reduced single-reference LAST-path proof over controlled 32x32 natural-ish fixtures.
- It proves RTL-owned raw-byte equality against the oracle and public-decoder reconstruction parity for those gates.
- It does not claim full AV1 completion, ASIC readiness, arbitrary natural-motion coverage, final 720p target readiness, or full removal of the `tb/` software debug writer.

## Next Slice

1. Widen beyond the controlled natural32 `NEWMV` / fractional q3 proof into less constrained, larger, and longer natural-motion clips while preserving RTL byte ownership.
2. Keep `natural32-ip-fractional-syntax-check`, `natural32-ip-newmv-syntax-check`, `natural32-ip-syntax-check`, `natural32-chroma-syntax-check`, `nonzero-chroma-syntax-check`, and `nonzero-chroma16-syntax-check` green as the short public-decoder gate family.
3. Continue the reduced LAST-only fractional/inter roadmap before expanding to compound references, richer partitioning, in-loop filters, rate control, or final-target validation.

## Regression Gates

- `make THREADS=16 BUILD_JOBS=16 natural32-ip-fractional-syntax-check` for the shortest controlled half-pel `NEWMV` public-decoder proof.
- `make THREADS=16 BUILD_JOBS=16 natural32-ip-newmv-syntax-check` for the controlled shifted non-zero-MV reduced LAST proof.
- `make THREADS=16 BUILD_JOBS=16 natural32-ip-syntax-check` for the 32x32 zero-MV IP residual proof.
- `make THREADS=16 BUILD_JOBS=16 natural32-chroma-syntax-check`, `nonzero-chroma-syntax-check`, and `nonzero-chroma16-syntax-check` for the current chroma syntax/recon proof family.
- `output/highdc_q1/` for strict large-DC coefficient ownership.
- `data/ac_probe_16x16_1f.yuv` at `qindex=240` when that asset is available in the checkout.
- `output/natural_focus_x640_y360_q128/` for strict natural `16x16` DC-only exactness.
- `data/natural_repeat64_x640_y360_2f.yuv` at `qindex=128` for larger natural-content zero-motion inter exactness.
- `data/natural_motion64_x640_y360_2f.yuv`, `data/natural_motion64_x640_y360_3f.yuv`, and `data/natural_motion32_x640_y360_3f.yuv` at `qindex=128` for reduced natural-motion inter exactness.
- `output/natural_motion64_x640_y360_2f_subpel2/`, `output/natural_motion64_x640_y360_7f_subpel2/`, and `output/natural_motion64_x640_y360_10f_subpel2/` as the syntax-only subpel guards.
- `output/natural_motion32_x640_y360_5f_fix1/`, `output/natural_motion64_x640_y360_5f_fix1/`, and `output/natural_motion64_x640_y360_6f_fix1/` as the shorter exact longer-motion guards.
- `output/natural_motion64_x640_y360_7f_fixmvref64/` and `output/natural_motion64_x640_y360_10f_progress70m/` as the repaired exact longer-motion guards.

## Local Notes

- `data/ac_probe_16x16_1f.yuv` is not present in this checkout, so the original exact-match `16x16` gate cannot be rerun locally yet.
- `data/tmp_probe_16x16_1f.yuv` is decode-clean but not byte-exact in this checkout; do not use it as the ownership gate.
- The full `10`-frame `64x64` natural-motion guard remains runtime-heavy after the subpel syntax step. The bounded `+progress_every=5000000 +timeout=70000000` run is still the reference command when checking that guard.
- The new natural32 fractional gate is intentionally controlled with `+me_newmv_limit=2`; widening beyond that limit is future feature work, not an already-closed full-motion claim.


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
