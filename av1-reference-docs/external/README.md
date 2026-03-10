# External AV1 References

This folder holds official external references pulled into the repo when the
local spec snapshot and SVT-AV1 source are not enough to resolve an active
syntax or verification blocker.

## Downloaded Sources

- `av1-spec.html`
  - Source: `https://aomediacodec.github.io/av1-spec/`
  - Purpose: searchable official AV1 spec HTML for frame/OBU syntax, inferred
    fields, and tile-group layout.
- `av1-spec-latest.pdf`
  - Source: `https://aomediacodec.github.io/av1-spec/av1-spec.pdf`
  - Purpose: latest official AV1 spec PDF snapshot from AOMedia.
- `ffmpeg-bitstream-filters.html`
  - Source: `https://ffmpeg.org/ffmpeg-bitstream-filters.html`
  - Purpose: official `trace_headers` documentation for header-level AV1
    inspection during debug.
- `ffmpeg-cbs-av1-syntax-template.html`
  - Source: `https://www.ffmpeg.org/doxygen/trunk/cbs__av1__syntax__template_8c_source.html`
  - Purpose: official FFmpeg AV1 syntax writer/parser source reference to
    confirm exact field ordering and inferred-vs-signaled behavior.
- `ffmpeg-cbs-av1-syntax-template.c`
  - Source: `https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/cbs_av1_syntax_template.c`
  - Purpose: raw FFmpeg AV1 CBS source for direct local grep/diff during
    frame-header and tile-syntax debug.
- `libaom-bitstream.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official encoder-side bitstream emission reference for
    `write_ref_frames`, `write_inter_mode`, `write_drl_idx`, and frame-header
    field ordering.
- `libaom-decodeframe.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official decoder-side frame/tile parsing reference when the local
    stream reaches tile-data corruption.
- `libaom-decodemv.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official MV decode reference for matching the writer-side MV
    payload semantics.
- `libaom-encodemv.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official MV encode reference. This clarified that `NEWMV` must not
    be used when the coded MV equals the selected reference predictor.
- `libaom-encodemv.h`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: supporting declarations for the MV encoder path.
- `libaom-entropymode.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: default inter/intra mode CDF reference, including `single_ref`,
    `newmv`, `zeromv`, and `refmv`.
- `libaom-entropymv.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: default NMV entropy tables used by `av1_encode_mv`.
- `libaom-entropymv.h`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: supporting declarations for MV entropy coding.
- `libaom-enums.h`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official mode-context bit layout such as `GLOBALMV_OFFSET`,
    `REFMV_OFFSET`, and the associated masks.
- `libaom-mv.h`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: MV structure/layout reference.
- `libaom-mvref_common.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official `mode_context` and ref-MV stack construction logic.
- `libaom-mvref_common.h`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: supporting declarations for the MV reference stack logic.
- `libaom-pred_common.c`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: official intra/inter and single-ref context functions.
- `libaom-pred_common.h`
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: supporting declarations and access helpers for prediction contexts.

## Current Findings

- `frame_obu()` byte-aligns after `frame_header_obu()` and before
  `tile_group_obu()`. The writer's byte-aligned frame-header handoff is correct.
- For full video headers, `disable_frame_end_update_cdf` is inferred when
  `disable_cdf_update == 1`; it is not always signaled as an explicit bit.
- `frame_reference_mode()` infers `reference_select = 0` for `KEY_FRAME` and
  `INTRA_ONLY_FRAME`.
- `global_motion_params()` emits no bits for `KEY_FRAME` and `INTRA_ONLY_FRAME`.
- The reduced mixed-sequence bootstrap cannot use `INTRA_ONLY_FRAME` with
  `refresh_frame_flags = 0xFF`. `aomdec` rejects that combination as
  non-conformant.
- The current reduced-conformance bootstrap is:
  - `INTRA_ONLY_FRAME`
  - refresh `LAST` only (`refresh_frame_flags = 0x01`)
  - map every single-ref header entry back to slot `0` in the reduced inter
    header until fuller reference management exists
- `av1_encode_mv()` in official libaom asserts that the coded MV difference is
  non-zero, so a predictor-equal MV must be emitted as `NEARESTMV` / `NEARMV`
  rather than `NEWMV`.
- The official libaom / AV1 `default_scan_8x8` order is `0, 8, 1, 2, ...`,
  not the earlier guessed `0, 1, 8, 16, ...`.
- In the reference intra predictor setup, one-sided edge availability is not
  filled with flat `128` for both directions:
  - if top is missing but left exists, `above[]` is filled from `left[0]`
  - if left is missing but top exists, `left[]` is filled from `above[0]`
  - if both are missing, the defaults are `above[]=127`, `left[]=129`,
    and `top_left=128`
- The `GLOBALMV` context bit in `mode_context` is tied to temporal/ref-MV logic
  in `mvref_common.c`; it should not be invented heuristically when the reduced
  frame header is already inferring `use_ref_frame_mvs = 0`.
- The older `aomdec` tile-data corruption on the reduced multi-frame path was
  cleared by tightening the reduced single-ref writer around:
  - top-right availability
  - reduced ref-MV scan geometry
  - avoiding future-block probes in the farther-row / farther-column scans
- After those fixes:
  - the old `17` / `18` non-zero-MV threshold is no longer the active blocker
  - the original `64x64` 2-frame debug inter case is decoder-clean again
  - the small `16x16` 2-frame inter case now decodes and matches `recon.yuv`
    exactly
- After applying the official scan order plus the reference edge-fill rules to
  the current sparse AC still-picture bring-up:
  - the `16x16` `ac_probe_16x16_1f.yuv` check at `qindex=240` now decodes and
    matches `recon.yuv` exactly
  - the remaining gap on that case is no longer software-writer correctness;
    it is moving the same sparse AC syntax subset onto the RTL-owned raw path
- The current ME core fix is local, not spec-derived, but was guided by the
  surrounding reference behavior:
  - candidate SAD must include the final sample before best-match update
  - valid search bounds can be derived directly from frame geometry instead of
    spending cycles on impossible candidates

## Active Debug Focus

- Keep using these sources to close the gap between:
  - the reduced decoder-clean software-writer path, and
  - the required RTL-owned final AV1 byte path.
- The immediate next goals are:
  - keep exact inter verification practical on the smallest debug clips while
    the exhaustive ME block remains expensive at `64x64` and above
  - move more final syntax ownership out of `tb/av1_bitstream_writer.h` and
    into the RTL bitstream path without regressing decoder cleanliness
