# Next Steps

## Active Direction

- Use `FULL_RTL_SCOPE.md` as the working backlog.
- Treat `PRE_ASIC_HANDOFF.md` and `P7_REFERENCE_BOUNDARY.md` as historical references only.
- Keep the reduced freeze out of any wording that could be read as project completion.

## Immediate Priorities

1. Keep the P1/P2 ownership and entropy/bitstream truth explicit.
2. Use P7/P8 to widen inter, ME, MV, and reference/GOP coverage.
3. Prefer the smallest representative proof that exercises the target behavior. For full-resolution BBB/inter validation, a 5-10 frame low-delay clip is the normal proof target; 240 frames is optional soak evidence only, not a completion requirement.
4. Finish P9-P12 only after the inter and reference lanes are stable.
5. Leave P13 ASIC readiness until the RTL feature matrix is actually complete.


## Current Full-Resolution BBB Finding

The old 240-frame target has been replaced by a representative 5-10 frame proof. Current 1280x720 low-delay BBB smoke (`+frames=5 +dc_only=0 +all_key=0 +key_interval=12`) is the right-sized proof and currently exposes the blocker: frame 0 encodes as KEY, frames 1-3 encode as INTER, but the promoted LAST reference collapses to flat luma (`avg=16 min=16 max=16`) and inter frames report `0/14400` coefficient-bearing blocks. Frame 4 then runs extremely slowly / times out. Treat this as the active P7/P8/P11 blocker before any longer soak.

## Validation Entry Points

- Use the row-specific gates listed in `FULL_RTL_SCOPE.md`.
- For historical freeze reruns, keep the single-threaded matrix invocation used by `PRE_ASIC_HANDOFF.md`.
