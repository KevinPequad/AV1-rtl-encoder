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

The old 240-frame target is no longer the next debug loop. After branch consolidation on `main`, the 64x64 low-delay gradient repro now completes multi-frame KEY->INTER runs with non-flat LAST references and nonzero inter coefficient blocks; the old flat-LAST collapse is not the current blocker.

Current 64x64 full-coeff NEWMV boundary: the checked-in reduced reference-stack/MV-prediction model is public-decoder clean for the unrestricted-request `tb/test_natural64_ip_fullcoeff_newmv_syntax.py` proof (`+frames=2`, `+dc_only=0`, `+all_key=0`, `+gop_mode=lowdelay_last`, `+key_interval=12`, gradient input, `+me_newmv_limit=255`) with no 64x64 hard cap and block 52 admitted as NEWMV. The companion legacy-plusarg probe `tb/test_natural64_ip_fullcoeff_newmv_boundary52_probe.py` now verifies `+me_allow_boundary52_newmv=1` is accepted as an ignored compatibility knob while the same block-52 NEWMV path stays public-decoder clean. Those gates prove RTL/software byte equality, FFmpeg/libdav1d decode-to-recon parity, and `aomdec` decode-to-recon parity.

The next unrestricted 64x64+ work is to widen beyond the two-frame 64x64 gradient fixture: keep exercising downstream reference-stack/MV-prediction behavior and multi-frame LAST refresh effects without treating this checkpoint as full inter/reference completion. The current public-clean natural64 full-coeff fixture verifies 43 NEWMV plus 3 NEARESTMV blocks with RTL/SW byte equality, FFmpeg/libdav1d decode-to-recon, and `aomdec` decode-to-recon; broader P7/P8 reference classes, GOP shapes, and full-resolution representative clips remain open.

2026-05-30 cron widening note: the first strict 3-frame version of that same unrestricted 64x64 gradient proof (`+frames=3`, `+dc_only=0`, `+all_key=0`, `+gop_mode=lowdelay_last`, `+key_interval=12`, `+me_newmv_limit=255`, `+dump_inter_summary=1`, `+dump_ref_summary=1`) keeps RTL/software OBU and IVF bytes equal, and frame 1 keeps the known public-clean `GLOBALMV:18 NEARESTMV:3 NEARMV:0 NEWMV:43` mix, but frame 2 currently misses FFmpeg/libdav1d decode-to-recon parity by two Cb bytes: frame 2 Cb `(x=5,y=19)` and `(x=9,y=19)` are decoder `+1` versus RTL recon, mapping to luma blocks 33 and 34. Frame 2's summary is `GLOBALMV:34 NEARESTMV:3 NEARMV:0 NEWMV:27` with only one nonzero luma-inter block, so the next technical debug target is the frame-2 chroma inter prediction/reconstruction path for those LAST-ref NEWMV blocks, not another cap raise.

## Validation Entry Points

- Use the row-specific gates listed in `FULL_RTL_SCOPE.md`.
- For historical freeze reruns, keep the single-threaded matrix invocation used by `PRE_ASIC_HANDOFF.md`.
