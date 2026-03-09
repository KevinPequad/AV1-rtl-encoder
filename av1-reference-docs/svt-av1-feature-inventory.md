# SVT-AV1 Encoder Feature Inventory

## Snapshot

- Reference repo scanned: `../../SVT-AV1`
- Snapshot commit: `3610ad762b9f634e149262895f9561d25711bbf6`
- Snapshot date: `2026-03-06`
- Primary references:
  - `../../SVT-AV1/Docs/Parameters.md`
  - `../../SVT-AV1/Docs/svt-av1_encoder_user_guide.md`
  - `../../SVT-AV1/Docs/svt-av1-encoder-design.md`
  - `../../SVT-AV1/Docs/CommonQuestions.md`
  - `../../SVT-AV1/Docs/Appendix-Rate-Control.md`
  - `../../SVT-AV1/Docs/Appendix-Mode-Decision.md`
  - `../../SVT-AV1/Source/Lib/Globals/enc_settings.c`

## How to read this document

This is not a raw dump of every SVT-AV1 flag. It is a feature inventory grouped by encoder function so we can decide what to copy into a real-time RTL design, what to simplify, and what to defer.

When docs disagree, treat `Parameters.md` and `Source/Lib/Globals/enc_settings.c` as the current ground truth. `CommonQuestions.md` is still useful, but it explicitly says some feature interactions were documented from an older code snapshot.

## 1. End-to-end encoder pipeline in SVT-AV1

SVT-AV1 implements a full AV1 encoder pipeline with these major stages:

- Resource coordination
- Picture analysis
- Picture decision / GOP formation
- Motion estimation
- Initial rate control
- Source-based operations
- Picture manager
- Rate control
- Mode decision configuration
- Mode decision
- Encode pass
- Deblocking loop filter
- CDEF
- Restoration filter
- Entropy coding
- Packetization

Why this matters for RTL:

- This is the cleanest top-level partition map for a hardware encoder.
- The minimum real-time subset is much smaller than the full SVT pipeline.
- The big hardware cost centers are motion estimation, partitioning/mode decision, transform/quant, entropy coding, and the in-loop filters.

## 2. Sequence, input, and output support

SVT-AV1 supports:

- Raw `yuv` and `y4m` input
- 8-bit and 10-bit YUV 4:2:0 workflows
- Configurable frame width, height, and frame rate
- AV1 profiles `main`, `high`, and `professional`
- Auto or explicit AV1 level selection
- Reconstructed frame output
- PSNR / SSIM stat reporting
- Still-picture AVIF mode

Real-time RTL relevance:

- Our current project target is narrower: 8-bit, 4:2:0, live pipeline, widths/heights constrained to a hardware-friendly grid.
- 10-bit, AVIF, and full profile/level coverage should be treated as later expansion items unless they are required by the bitstream packer from day one.

## 3. GOP, picture types, and latency behavior

SVT-AV1 has these picture-structure features:

- Low-delay prediction structure (`--pred-struct 1`)
- Random-access prediction structure (`--pred-struct 2`)
- Hierarchical prediction with `--hierarchical-levels 2-5` (3 to 6 temporal layers)
- Open GOP / forward-frame refresh (`--irefresh-type 1`)
- Closed GOP / key-frame refresh (`--irefresh-type 2`)
- Configurable GOP size (`--keyint`)
- Forced keyframe insertion at explicit frame or time positions
- Lookahead up to 120 frames
- Dynamic GOP (`--enable-dg`)
- Startup mini-GOP sizing (`--startup-mg-size`)
- Real-time communication mode (`--rtc`)

Important current-source constraints:

- `--rtc` is for real-time low-delay usage.
- Super-resolution is not supported in low-delay mode.
- Overlay frames are not supported in low-delay mode.
- Scene change detection exists as a heuristic, but current SVT-AV1 does not auto-insert keyframes on scene changes.

Real-time RTL recommendation:

- Start with low-delay only.
- Start with closed GOP only.
- Use fixed GOP length.
- Defer random-access, deep hierarchy, dynamic GOP, and any feature that depends on future-frame reordering.

## 4. Rate control and quality control features

SVT-AV1 rate-control surface is large. The main supported capabilities are:

- CQP / CRF style single-pass mode (`--rc 0`)
- VBR (`--rc 1`)
- CBR (`--rc 2`)
- Target bitrate (`--tbr`)
- Max bitrate / capped CRF (`--mbr`)
- Min / max QP clamps
- Per-frame QP file input
- Fixed qindex offset tables by frame type / temporal layer
- Chroma and DC/AC qindex offsets
- AQ modes:
  - `0`: off
  - `1`: variance-based segmentation AQ
  - `2`: delta-q / prediction-efficiency AQ
- Variance Boost
- ROI map driven block-level QP offsets
- GOP-constrained rate control
- Buffer model controls for CBR
- Recode loop control
- Quantization matrices
- Lambda scale factors
- Temporal filtering strength control
- Luminance-based frame QP bias
- Sharpness bias
- AC-bias psychovisual control
- Multi-pass support for VBR and some CRF workflows

Architectural notes from SVT-AV1:

- Rate control uses picture analysis, lookahead, packetization feedback, and TPL data.
- QP can vary at frame level, SB level, and indirectly at block decision level via lambda.
- Re-encode / recode loops are used when bitrate matching needs correction.

Real-time RTL recommendation:

- Phase 1 should use fixed QP or very simple CBR.
- Phase 2 can add frame-level RC and maybe SB-level AQ.
- Full VBR, two-pass, ROI maps, qindex offset tables, quant matrices, and recode loops should be deferred until the basic datapath is stable.

## 5. Core coding tools SVT-AV1 uses

### 5.1 Block structure and partitioning

SVT-AV1 uses:

- 64x64 and 128x128 superblocks
- Square and non-square partitions
- Minimum block sizes down to 4x4 on some presets
- Staged partition decision refinement
- Depth pruning / depth removal
- Lighter mode-decision paths for easier blocks

RTL recommendation:

- Use a reduced partition set first.
- Start with 64x64 superblocks.
- Start with a floor of 8x8 or 16x16 unless quality demands smaller blocks.
- Add non-square partitions only after square partitions are working cleanly.

### 5.2 Intra prediction features

SVT-AV1 uses these intra tools:

- DC
- Directional angular modes
- Smooth, Smooth H, Smooth V
- Paeth
- Chroma from Luma (CfL)
- Filter intra
- Recursive intra search

RTL recommendation:

- Phase 1: DC + directional + Paeth.
- Phase 2: Smooth modes + CfL.
- Defer filter intra and deeper recursive search until the baseline encoder is correct.

### 5.3 Inter prediction features

SVT-AV1 uses these inter tools:

- Single-reference prediction
- Compound-reference prediction
- NEWMV / NEAREST / NEAR style candidate classes
- Motion Field Motion Vectors (MFMV)
- Inter-intra prediction
- Overlapped Block Motion Compensation (OBMC)
- Local warped motion
- Global motion
- Distance-weighted compound prediction
- Difference-weighted compound prediction
- Wedge prediction

RTL recommendation:

- Phase 1: single-reference translational inter only.
- Phase 2: add better MV predictors and maybe limited compound support.
- Defer OBMC, warped motion, global motion, wedge, and weighted compound modes until much later.

### 5.4 Motion estimation and temporal modeling

SVT-AV1 includes:

- Full-pel motion estimation
- Hierarchical motion estimation
- Sub-pel refinement
- Interpolation-filter search
- TPL temporal dependency modeling
- Temporal filtering for ALT-REF generation

RTL recommendation:

- Phase 1: integer + fractional translational ME only.
- Phase 2: improve search quality and predictor reuse.
- Defer TPL and temporal filtering until the low-delay baseline is done.

### 5.5 Transform, quantization, and RD tools

SVT-AV1 includes:

- Transform size search
- Transform type search
- Max transform size control
- Quantization matrices
- SB and block lambda tuning
- Psychovisual bias controls

RTL recommendation:

- Start with a reduced transform set.
- Strong candidate first cut: DCT-only or a very small AV1-conformant transform subset if the goal is bitstream bring-up.
- Full transform-type search is expensive and should be staged in later.

## 6. In-loop filtering and post-processing tools

SVT-AV1 supports:

- Deblocking loop filter
- CDEF
- Loop restoration filter
  - Wiener
  - Self-guided restoration
- Film grain synthesis
- Adaptive film grain block sizing

RTL recommendation:

- Phase 1: basic deblock only, or even no in-loop filtering for early bring-up if the decoder path stays conformant.
- Phase 2: add CDEF.
- Phase 3: add restoration.
- Treat film grain synthesis as software-side or very-late hardware work unless a product requirement forces it earlier.

## 7. Special AV1 stream features

SVT-AV1 exposes these AV1-specific stream features:

- Tile rows and tile columns
- ALT-REF frames
- Overlay frames
- Super-resolution
- Reference scaling / resize
- S-frames
- Forced keyframes during encode session
- Mid-session bitrate and resolution updates through the API

Notes for real-time logic:

- Tiles can help throughput, but they also complicate state management and can reduce quality if overused.
- ALT-REF and overlays are useful for quality but are not aligned with a simple low-delay RTL first milestone.
- Super-resolution and resize are substantial architectural changes, not small options.
- Mid-stream resolution change is mostly a control-plane problem plus buffer reset logic, so it can be added later without changing the whole coding toolset.

## 8. Screen-content and content-adaptive tools

SVT-AV1 has explicit support for screen-content workflows:

- Screen content mode selection (`--scm`)
- Intra Block Copy (IBC)
- Palette prediction
- Anti-alias-aware screen-content detection mode

RTL recommendation:

- Defer all screen-content tools in the first video-camera / natural-video encoder.
- Revisit only if the target workload includes desktop capture, UI, remote desktop, or game streaming.

## 9. Metadata and signaling support

SVT-AV1 can signal:

- Color primaries
- Transfer characteristics
- Matrix coefficients
- Full vs studio range
- Chroma sample position
- Mastering display metadata
- Content light metadata

RTL recommendation:

- Metadata signaling is cheap compared with core coding tools.
- This can often be implemented early in the packetizer even if the core encoder is still very minimal.

## 10. Features that best match our current project constraints

From this repo's current rules, our first encoder target is:

- 8-bit
- 4:2:0
- Resolution multiple of 16
- I-frames and P-frames only
- Real-time oriented

The SVT-AV1 features that align best with that target are:

- Low-delay prediction structure
- Closed GOP keyframe refresh
- Fixed GOP length
- Single-reference inter prediction
- Basic translational motion estimation
- Basic intra prediction
- Reduced partition tree
- Reduced transform set
- Fixed-QP or simple CBR control
- Entropy coding and packetization
- Optional basic deblock

The SVT-AV1 features that do **not** align with our first milestone are:

- Random-access / deep temporal hierarchy
- ALT-REF / overlay workflows
- Two-pass RC
- Full psychovisual tuning system
- Super-resolution
- Reference scaling
- S-frames
- Film grain synthesis
- IBC / palette / screen-content modes
- OBMC / warped motion / global motion / complex compound modes

## 11. Recommended implementation order

### P0: first bring-up

- Low-delay only
- I and P frames only
- 8-bit 4:2:0 only
- Fixed resolution
- Fixed QP
- Square partitions only
- Basic intra modes
- Single-reference translational inter
- Basic transform + quant + entropy coding
- Bitstream packetization

### P1: first real-time usable encoder

- Better partition search
- Fractional-pel refinement
- Better intra mode coverage
- Simple CBR
- Basic deblock
- Better MV prediction reuse

### P2: quality upgrades that still fit real-time

- AQ / SB-level QP modulation
- Limited lookahead
- CDEF
- More transform choices
- Limited non-square partitions
- Maybe tiles if throughput requires them

### P3: VOD-class or advanced feature set

- Deep hierarchy
- Random access
- ALT-REF / overlays
- Superres / resize
- Restoration filter
- Complex psychovisual tuning
- Screen-content tools
- Film grain synthesis
- Multi-pass RC

## 12. Practical conclusion

SVT-AV1 is a useful reference for three things:

- Which AV1 coding tools exist and how they are grouped
- Which tools are actually worth enabling for low-delay vs high-efficiency encoding
- How to decompose the encoder into pipeline stages

For our RTL design, the right move is **not** to mirror the full SVT-AV1 feature set. The right move is to copy the low-delay subset first, get a correct conformant encoder running, and then pull selected quality tools from SVT-AV1 in priority order.

## 13. Suggested reference configs to emulate first

If we want SVT-AV1 reference runs that are closest to the intended RTL shape, start from configs like:

- Low-delay prediction structure
- Closed GOP keyframes
- No overlays
- No superres
- No screen-content tools
- No film grain
- Fixed QP or simple CBR
- Modest preset with real-time focus

That gives us a reference behavior that matches the current RTL target instead of a VOD-oriented encoder profile.
