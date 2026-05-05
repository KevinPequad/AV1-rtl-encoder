# AV1 RTL Encoder Project Plan

## Program Goal

The active program goal is full feature-complete RTL AV1.

Historical checkpoints:

- `PRE_ASIC_HANDOFF.md` records the reduced pre-ASIC freeze on canonical `main` at `8994490d209a745fb157e999af616b49de2c6ce1`.
- `P7_REFERENCE_BOUNDARY.md` records the historical reduced LAST-only inter boundary.

## Source of Truth

- `FULL_RTL_SCOPE.md` — exact P0-P13 scope matrix derived from `av1-reference-docs/svt-av1-feature-inventory.md` and `/home/chudpc/.hermes/kanban/workspaces/t_f3708c82/av1_feature_gap_matrix.md`
- `PRE_ASIC_HANDOFF.md` — historical freeze marker only
- `P7_REFERENCE_BOUNDARY.md` — historical boundary checkpoint only

## Execution Order

1. Follow the row order in `FULL_RTL_SCOPE.md`.
2. Keep the first ownership checks honest: P1/P2 syntax, raw bytes, and decoder parity.
3. Use P7/P8 to finish inter, ME, MV syntax, and reference/GOP control.
4. Close P9-P12 only once the inter and reference lanes are stable.
5. Treat P13 as downstream ASIC readiness, not feature-completion work.

## Notes

- The freeze docs are historical references, not the active backlog.
- Any future doc change must preserve the split between active scope (`FULL_RTL_SCOPE.md`) and historical checkpoint files.
