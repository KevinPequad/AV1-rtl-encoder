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

Current 64x64 full-coeff NEWMV boundary: the checked-in reduced reference-stack/MV-prediction model is public-decoder clean through the capped `tb/test_natural64_ip_fullcoeff_newmv_syntax.py` proof (`+frames=2`, `+dc_only=0`, `+all_key=0`, `+gop_mode=lowdelay_last`, `+key_interval=12`, gradient input, `+me_newmv_limit=255`, effective 45-motion cap with block 52 deliberately guarded to GLOBALMV). That gate proves RTL/software byte equality, FFmpeg/libdav1d decode-to-recon parity, and `aomdec` decode-to-recon parity.

The next unrestricted 64x64+ blocker is still the reduced reference-stack/MV-prediction/recon model, not container packaging: an unguarded block-52 NEWMV run kept RTL/SW OBU and IVF bytes equal but failed FFmpeg/libdav1d and `aomdec` decode-to-recon at frame 1 block8 52 (`first_mismatch_offset=9248`, decoded luma `0xfe` vs RTL recon `0xff`, 96 bytes mismatched across Y/Cb/Cr). The block-52 summary was `mv=(64,-128) ref=(0,0) near=(128,-128) mode=NEWMV mode_ctx=84` with candidate stack `cand0=(64,-128,w=644) cand1=(0,0,w=648) cand2=(128,-128,w=648)`, so the checked-in gate now pins that boundary block to GLOBALMV and admits the next safe non-zero motion block. Fix block-52 NEWMV/recon parity before removing the guard, claiming unrestricted 64x64+, or returning to full-resolution BBB soak.

## Validation Entry Points

- Use the row-specific gates listed in `FULL_RTL_SCOPE.md`.
- For historical freeze reruns, keep the single-threaded matrix invocation used by `PRE_ASIC_HANDOFF.md`.
