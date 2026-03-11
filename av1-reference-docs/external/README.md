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
- local upstream AOM reference clone and `inspect` build
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: strict decoder/accounting validation with `CONFIG_ACCOUNTING=1`
    and `CONFIG_INSPECTION=1` when ffmpeg/libdav1d smoke decode is not
    sufficient to trust a syntax path.
- local `av1/common/token_cdfs.h` dump from the upstream AOM clone
  - Source: `https://aomedia.googlesource.com/aom/`
  - Purpose: exact official TX_8X8 and TX_4X4 coefficient CDF source used by
    `scripts/gen_av1_tx8x8_qctx_tables.py` to regenerate qctx-selected tables
    for the software debug writer and the RTL-owned coeff path.

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
- On the full video keyframe header path, `refresh_frame_context` is parsed
  before `tile_info` whenever `reduced_still_picture_hdr == 0` and
  `disable_cdf_update == 0`.
- In the reference intra predictor setup, one-sided edge availability is not
  filled with flat `128` for both directions:
  - if top is missing but left exists, `above[]` is filled from `left[0]`
  - if left is missing but top exists, `left[]` is filled from `above[0]`
  - if both are missing, the defaults are `above[]=127`, `left[]=129`,
    and `top_left=128`
- In libaom's directional intra path, edge filtering and edge upsampling are
  both skipped when `enable_intra_edge_filter == 0`; upsampling is not an
  independent tool bit.
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
  - the strict `16x16` 2-frame flat repeated-frame IP ownership repro is now
    byte-exact between the software-owned and raw RTL-owned OBU/IVF outputs
  - the last remaining raw-path tile-data drift on that repro was the reduced
    single-ref `cmp_ctx=2` / branch-0 CDF entry on the RTL path; the correct
    ICDF is `3024`, not the earlier `5024`
- After applying the official scan order plus the reference edge-fill rules to
  the current sparse AC still-picture bring-up:
  - the `16x16` `ac_probe_16x16_1f.yuv` check at `qindex=240` now decodes and
    matches `recon.yuv` exactly
  - that first sparse subset is now on the RTL-owned raw path for the reduced
    case where only `qcoeff[0]` and `qcoeff[1]` are nonzero and `qcoeff[8] == 0`
  - the reference-backed reduced symbol sequence for that subset is:
    - `eob_multi64` symbol `2` (`eob=3`)
    - one `eob_extra` symbol with value `0`
    - `coeff_base_eob` at scan `c=2`, `pos=1`, context `1`
    - zero `coeff_base` at scan `c=1`, `pos=8`, context `1`
    - DC sign with the normal neighbor-derived context
    - AC sign for the EOB coefficient at flat index `1`
  - the first dense low-order subset on that same probe is now also on the RTL
    raw path for the reduced case with nonzeros at flat indices `0`, `8`, `1`,
    and `10`
  - the reference-backed reduced symbol sequence for that denser subset is:
    - `eob_multi64` symbol `4` (`eob=9`)
    - one context-2 `eob_extra` symbol with value `0`
    - two trailing direct `0` bits for the remaining `eob_extra` payload
    - `coeff_base_eob` at scan `c=8`, `pos=10`, context `1`
    - zero `coeff_base` symbols at scan `c=7..3` with contexts `6,6,6,7,7`
    - `coeff_base` at scan `c=2`, `pos=1`, context `2`, symbol `2`
    - `coeff_base` at scan `c=1`, `pos=8`, context `2`, symbol `1`
    - DC sign plus AC signs for scan positions `1`, `2`, and `8`
- Directional intra reference availability matters on the same still-picture
  probe:
  - real top-right extension samples were required to clear the lower-left
    directional-block mismatch on the `qindex=224` debug case
  - bottom-left extension cannot be enabled blindly on the current fixed
    `8x8` / `TX_8X8` raster-order subset because that reads future pixels from
    blocks that are not reconstructed yet
  - with top-right enabled, bottom-left still disabled, and directional edge
    upsampling kept off while `enable_intra_edge_filter=0`, the `qindex=240`
    exact-match probe is bit-exact again on both the still-picture and
    video-keyframe outputs
- The current ME core fix is local, not spec-derived, but was guided by the
  surrounding reference behavior:
  - candidate SAD must include the final sample before best-match update
  - valid search bounds can be derived directly from frame geometry instead of
    spending cycles on impossible candidates
- The local upstream AOM `inspect` build is now the strict syntax oracle for
  the current coeff/tile mismatch:
  - it rejects some streams that ffmpeg/libdav1d will still smoke-decode
  - use it before trusting any new exact-match claim on a broadened syntax path
- The official qctx-selected coefficient tables matter on the current reduced
  TX_8X8 path:
  - the older hardcoded `qctx=3` tables in both the software debug writer and
    the RTL-owned generic coeff path were the root cause of the strict
    `output/highdc_q1/` first-block mismatch
  - regenerating those tables from upstream AOM `token_cdfs.h` and selecting
    them from `qindex` fixed the `qindex=1` large-DC repro
  - AOM inspection now parses all four intended `highdc_q1` blocks as
    `tx_size=1`, `eob=1`, with the expected Golomb tail
  - ffmpeg decode now matches `recon.yuv` bit-for-bit on that strict repro
- The stricter AOM decoder now splits the remaining blockers into two separate
  issues:
  - `qindex=0`:
    - AOM inspection shows `tx_size=0` (`TX_4X4`) on the failing cases
    - the current reduced encoder path still emits `TX_8X8` coefficient syntax
    - treat `qindex=0` as a separate lossless / `TX_4X4` implementation gap
    - until that path is implemented, requested `qindex=0` runs clamp to
      effective `qindex=1` in both the testbench and RTL top-level so the
      supported reduced subset stays valid
  - `qindex=1+`:
    - the old `highdc_q1` first-block mismatch is fixed, so it should now be
      used as a strict large-DC regression guard, not as the active blocker
    - the remaining work is extending the current reduced generic-coeff subset
      beyond the verified exact-match probes into less constrained dense blocks,
      then continuing the same ownership move into broader block syntax

## Active Debug Focus

- Keep using these sources to close the gap between:
  - the reduced decoder-clean software-writer path, and
  - the required RTL-owned final AV1 byte path.
- The immediate next goals are:
  - keep exact inter verification practical on the smallest debug clips while
    the exhaustive ME block remains expensive at `64x64` and above
  - move more final syntax ownership out of `tb/av1_bitstream_writer.h` and
    into the RTL bitstream path without regressing decoder cleanliness
  - use `data/ac_probe_16x16_1f.yuv` at `qindex=240` as the first exact-match
    regression gate and `output/highdc_q1/` as the strict large-DC guard
  - extend the current reduced non-DC ownership subset into larger-magnitude
    coefficient tails and less constrained dense blocks using the same
    reference traces before touching broader tile grammar
