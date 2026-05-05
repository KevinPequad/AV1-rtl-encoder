# Next Steps

## Completed

- The pre-ASIC AV1 baseline is frozen on canonical `main` at `8994490d209a745fb157e999af616b49de2c6ce1`; the single-threaded current matrix passed 21/21 gates under `THREADS=1 BUILD_JOBS=1`, and `PRE_ASIC_HANDOFF.md` captures the supported scope, exclusions, regression commands, and ASIC-readiness blockers.
- P7 reference boundary remains locked by `make bitstream-check WIDTH=16 HEIGHT=16`, `make gop-lifecycle-syntax-check`, `make natural64-ip-mode-context-syntax-check`, and `make natural64-ip-fractional-syntax-check`.
- The Chud PC 2 smoke / natural-motion proof commands remain the regression surface for future fixes:
  - `make THREADS=1 BUILD_JOBS=1 entropy-check`
  - `make THREADS=1 BUILD_JOBS=1 bitstream-check WIDTH=16 HEIGHT=16`
  - `make THREADS=1 BUILD_JOBS=1 natural32-ip-syntax-check`
  - `make THREADS=1 BUILD_JOBS=1 natural32-ip-newmv-syntax-check`
  - `make THREADS=1 BUILD_JOBS=1 natural32-ip-fractional-syntax-check`
  - `make THREADS=1 BUILD_JOBS=1 gop-lifecycle-syntax-check`
  - `make THREADS=1 BUILD_JOBS=1 natural64-ip-mode-context-syntax-check`
  - `make THREADS=1 BUILD_JOBS=1 natural64-ip-fractional-syntax-check`

## What Remains

- ASIC-readiness only: lint, synthesis-top-smoke, memory macro / black-box strategy, timing closure, physical signoff, and packaging/tapeout planning.
- No new AV1 feature work should be started without an explicit scope reset.

## Blockers

- No ASIC synthesis or signoff evidence yet.
- The final `1280x720 @ 24 fps` Big Buck Bunny target remains a system-integration milestone, not a current feature gap.

## Exact Next Command Or File To Edit

- `PRE_ASIC_HANDOFF.md` if the frozen baseline changes.
- Otherwise start the first ASIC-flow task.
