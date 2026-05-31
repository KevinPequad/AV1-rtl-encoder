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

2026-05-30 cron widening note: the first strict 3-frame version of that same unrestricted 64x64 gradient proof (`+frames=3`, `+dc_only=0`, `+all_key=0`, `+gop_mode=lowdelay_last`, `+key_interval=12`, `+me_newmv_limit=255`, `+dump_inter_summary=1`, `+dump_ref_summary=1`) keeps RTL/software OBU and IVF bytes equal, and frame 1 keeps the known public-clean `GLOBALMV:18 NEARESTMV:3 NEARMV:0 NEWMV:43` mix, but frame 2 currently misses both FFmpeg/libdav1d and `aomdec` decode-to-recon parity by the same two Cb bytes: frame 2 Cb `(x=5,y=19)` and `(x=9,y=19)` are decoder `+1` versus RTL recon, mapping to luma blocks 33 and 34. Frame 2's summary is `GLOBALMV:34 NEARESTMV:3 NEARMV:0 NEWMV:27` with only one nonzero luma-inter block; both failing pixels are in the frame-2 LAST-ref NEWMV region (`blk=33`/`34`). The next technical debug target is the frame-2 chroma inter prediction/reconstruction path for those blocks, not another cap raise.

2026-05-31 follow-up: the 3-frame probe now also pins the unscaled identity-reference chroma origin for `blk=33/34` to base `(11,11)` / phase `(8,0)` and records that the scaled-reference `+8` chroma-siting derivation would instead land at base `(12,11)` / phase `(0,8)`. Do not try to fix this blocker by adding a blanket +8 offset in `rtl/av1_chroma_inter_pred.v`; the remaining delta still needs a decoded-MV/ref-stack or filter-selection explanation.

2026-05-31 force-integer MV follow-up: the same probe now mirrors libaom force_integer_mv component decode for blk=33/34 and proves the NEWMV residuals round-trip exactly to (104,-128) / (40,-128) from their current ref MVs. It also compares small-block and full regular-filter coefficients at the exact frame-1 Cb taps; both phase-8 filters still predict the RTL 0xA3 value while phase 9 predicts the public-decoder 0xA4 value. The probe now also captures the RTL chroma pixel predictor/recon vectors for blk33/34: blk33 local Cb sample (1,3) is pure prediction/recon 0xA3 with no Cb residual, and blk34 local Cb sample (1,3) reconstructs 0xA7 after the single DC residual while both public decoders reconstruct 0xA4/0xA8 respectively. That narrows the remaining public-decoder +1 Cb delta away from MV component coding, away from a simple small-vs-full regular-filter swap, and into decoded syntax/ref-sample interpretation around blk33/34.

2026-05-31 source-backed chroma-origin follow-up: `tb/test_natural64_ip_fullcoeff_newmv_3frame_probe.py` now also mirrors current libaom `reconinter.h:init_subpel_params()` q10 unscaled chroma setup against the reduced spec derivation. Both keep the failing frame-2 Cb samples at base `(11,11)` / phase `(8,0)`, while the scaled-path/phase-9 alternatives only remain contrasts. The next debug step should extract the public decoder's actual decoded MV/ref-sample inputs for blk33/34 rather than patching `rtl/av1_chroma_inter_pred.v` with an unsupported phase offset.

2026-05-31 public-decoder block-vector follow-up: the 3-frame probe now pins the full public-decoder Cb 4x4 vectors for frame-2 blk33/34. FFmpeg/libdav1d and `aomdec` agree with RTL on every neighboring Cb sample; the only local delta in each block is sample `(1,3)`, and blk34 still carries the same +4 DC residual as RTL. This moves the next debug step away from Cb residual scan/transform/frame-buffer corruption and toward the single inter predictor input/phase actually used by public decoders at that sample.

2026-05-31 blanket-phase contrast follow-up: the 3-frame probe now computes full blk33/34 Cb predictor vectors from the actual frame-1 reference taps for both phase 8 and the tempting phase 9 contrast. Phase 9 matches the public decoder at the single failing local sample, but it would introduce six other Cb predictor deltas per block, so a blanket phase bump is now executable-rejected; the next step remains extracting the precise public-decoder per-sample/subpel/ref interpretation for blk33/34.

2026-05-31 round-offset anti-workaround note: a temporary rebuilt RTL probe changing `rtl/av1_chroma_inter_pred.v` horizontal filter rounding from `+64` to `+68` did flip the local blk33/34 sample from RTL 0xA3/0xA7 to the public 0xA4/0xA8 values, but it did not solve the 3-frame stream. FFmpeg/libdav1d and `aomdec` still agreed with each other and then mismatched RTL recon at 13 bytes (first offsets 10476..10479 and later frame-2 Cb/Cr samples), so round-offset bias is now explicitly rejected as another blanket workaround. Keep pursuing the actual public-decoder per-sample/subpel/ref interpretation.

## Validation Entry Points

- Use the row-specific gates listed in `FULL_RTL_SCOPE.md`.
- For historical freeze reruns, keep the single-threaded matrix invocation used by `PRE_ASIC_HANDOFF.md`.
