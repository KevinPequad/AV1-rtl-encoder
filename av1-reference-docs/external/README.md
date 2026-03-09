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

## Current Findings

- `frame_obu()` byte-aligns after `frame_header_obu()` and before
  `tile_group_obu()`. The writer's byte-aligned frame-header handoff is correct.
- For full video headers, `disable_frame_end_update_cdf` is inferred when
  `disable_cdf_update == 1`; it is not always signaled as an explicit bit.
- `frame_reference_mode()` infers `reference_select = 0` for `KEY_FRAME` and
  `INTRA_ONLY_FRAME`.
- `global_motion_params()` emits no bits for `KEY_FRAME` and `INTRA_ONLY_FRAME`.
- The current mixed-sequence mismatch is not a top-level OBU/container parse
  error. `trace_headers` parses the sequence header and frame headers cleanly.

## Active Debug Focus

- Keep using these sources to close the gap between:
  - the verified all-key reduced-still path, and
  - the mixed-sequence video path needed for real IP output.
- The immediate goal is decoded output matching RTL reconstruction on the
  sequence path before expanding beyond zero-motion inter blocks.
