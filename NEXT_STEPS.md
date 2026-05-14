# Next Steps

## Active Direction

- Use `FULL_RTL_SCOPE.md` as the working backlog.
- Treat `PRE_ASIC_HANDOFF.md` and `P7_REFERENCE_BOUNDARY.md` as historical references only.
- Keep the reduced freeze out of any wording that could be read as project completion.

## Immediate Priorities

1. Keep the P1/P2 ownership and entropy/bitstream truth explicit.
2. Use P7/P8 to widen inter, ME, MV, and reference/GOP coverage.
3. Prefer the smallest representative proof that exercises the target behavior. For full-resolution BBB/inter validation, 2 frames is acceptable when a KEY→INTER transition proves the target behavior; use more frames only when needed to exercise references/GOP/quality behavior. 240 frames is optional soak evidence only, not a completion requirement.
4. Finish P9-P12 only after the inter and reference lanes are stable.
5. Leave P13 ASIC readiness until the RTL feature matrix is actually complete.


## Current Full-Resolution BBB Finding

The old 240-frame target is no longer the next debug loop. After branch consolidation on `main`, the 64x64 low-delay gradient repro now completes 5 frames with non-flat LAST references and nonzero inter coefficient blocks; the old flat-LAST collapse is not the current blocker. The active blocker is decoder/recon correctness for non-DC inter coefficient streams: `64x64`, `+frames=2`, `+dc_only=0`, `+all_key=0`, `+gop_mode=lowdelay_last`, `+key_interval=12`, gradient input, produces RTL/SW byte-identical OBU/IVF but public decoders reject or stop at the inter frame (`aomdec`: corrupt tile data). The `32x32` equivalent decodes both frames but decoded YUV differs from `recon.yuv`, so fix the reduced inter residual/coefficient syntax and recon parity before returning to full-resolution BBB soak.

## Validation Entry Points

- Use the row-specific gates listed in `FULL_RTL_SCOPE.md`.
- For historical freeze reruns, keep the single-threaded matrix invocation used by `PRE_ASIC_HANDOFF.md`.
