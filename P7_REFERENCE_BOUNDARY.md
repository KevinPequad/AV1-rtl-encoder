# P7 Reference Boundary Checkpoint

Canonical checkpoint:

- commit `8994490d209a745fb157e999af616b49de2c6ce1` on `main`
- `origin/main` is pushed
- validated on Chud PC 2 with `THREADS=1 BUILD_JOBS=1`; the single-threaded current matrix passed 21/21 gates

Historical context:

This records the reduced LAST-only inter boundary that supported the pre-ASIC freeze. It is a historical staging milestone, not the active program goal. The active full-scope plan lives in `FULL_RTL_SCOPE.md`.

Owned now:

- single-reference LAST-only inter prediction
- LAST-path motion modes already proven by the current inter fixtures: GLOBALMV, NEARESTMV, NEARMV, NEWMV, ZEROMV
- 64x64 low-delay LAST-only lifecycle proof
- 64x64 LAST-path mode/context syntax gate (syntax/byte-ownership only)
- 64x64 fractional-q3 NEWMV proof
- sequence/frame headers that legally disable compound, masked, inter-intra, warped, and order-hint tools for this checkpoint

Deferred for a future full-inter lane:

- LAST2, LAST3, GOLDEN, BWDREF, ALTREF, ALTREF2
- compound prediction of any family, including weighted, wedge, and masked variants
- MFMV / use_ref_frame_mvs
- OBMC
- warped/global motion beyond the reduced signaling
- inter-intra
- random access, open GOP, overlay, hierarchical GOP, and multi-ref refresh policy

Boundary gates:

- `make bitstream-check WIDTH=16 HEIGHT=16`
- `make gop-lifecycle-syntax-check`
- `make natural64-ip-mode-context-syntax-check`
- `make natural64-ip-fractional-syntax-check`
- existing `natural32-ip-syntax-check`, `natural32-ip-newmv-syntax-check`, and `natural32-ip-fractional-syntax-check`

Public-decoder proof gates must come from the RTL raw OBU / IVF bytes with FFmpeg/libdav1d and aomdec decode-to-recon parity. Header-only gates such as `make bitstream-check WIDTH=16 HEIGHT=16` and `make natural64-ip-mode-context-syntax-check` remain separate syntax regressions and do not claim decoder parity. The testbench may compare against the software oracle, but it must not repair the public-decoder stream.

This checkpoint does not freeze the long-term goal; it only records the currently owned LAST-only boundary so the next AV1 inter lane can widen from a verified baseline. See `FULL_RTL_SCOPE.md` for the current program scope and `PRE_ASIC_HANDOFF.md` for the historical freeze package.

## 2026-05-30 unrestricted block-52 full-coeff checkpoint

The earlier cap-46/block-52 exploratory failure is resolved on the cron progress branch by the checked-in reference-stack/MV-selection alignment. The active 64x64 full-coeff LAST/NEWMV public-clean boundary now admits frame-1 block 52 as NEWMV under `+me_newmv_limit=255` with no separate 64x64 hard cap. The current expected summary is `GLOBALMV:18 NEARESTMV:3 NEARMV:0 NEWMV:43`, with block 52 reported as `mv=(64,-128) ref=(128,-128) near=(0,0) mode=NEWMV` and candidate-stack entries including `cand0=(64,-128,w=644)`, `cand1=(128,-128,w=648)`, and `cand2=(0,0,w=648)`.

Verified gates for this checkpoint:

- `THREADS=1 BUILD_JOBS=1 make natural64-ip-fullcoeff-newmv-syntax-check`
- `THREADS=1 BUILD_JOBS=1 make natural64-ip-fullcoeff-newmv-boundary52-probe-check`

Both gates require RTL/software raw OBU and IVF byte equality plus FFmpeg/libdav1d and `aomdec` decode-to-recon parity. This is still only a reduced 64x64 two-frame LAST-only checkpoint; it does not close the broader P7/P8 multi-reference/GOP or P13 ASIC-readiness work.

