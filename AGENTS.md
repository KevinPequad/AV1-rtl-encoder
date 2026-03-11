# AV1 RTL Encoder - Project Rules

## Mission
- The mission of this repo is to finish a full RTL AV1 encoder.
- The finished encoder must generate a decodable AV1 bitstream from the RTL byte path itself.
- The testbench may dump bytes, package media, and run decode and comparison checks, but it must not author the final AV1 syntax on behalf of the RTL for project completion.
- Do not mark the encoder complete until the decoded AV1 output has been verified and the bitstream came from the RTL output path.
- Do not stop at subset milestones, decode-only smoke wins, or documentation updates while major feature or ownership gaps remain.
- Work through the roadmap in `av1-reference-docs/svt-av1-feature-inventory.md` phase by phase until the encoder is complete or a concrete technical blocker is reached and documented precisely.

## Execution Policy
- Continue from the current repo state until the full encoder roadmap is complete.
- Default behavior: continue work autonomously across implementation, build, simulation, debug, verification, and the next backlog item without waiting for confirmation between routine substeps.
- Do not stop for milestones, progress updates, partial success, or clean checkpoints.
- Do not stop at analysis, plans, documentation, or a single successful build if code changes and verification work are still possible.
- Do not treat a user-requested side task or maintenance task as a terminal event if the main encoder roadmap is still active.
- Examples of non-terminal side tasks:
  - updating `.gitignore`
  - updating `README.md` or `AGENTS.md`
  - cleaning build scripts or helper scripts
  - adding local debug utilities
  - small workflow, tooling, or repo-hygiene fixes
- After completing a side task, immediately resume the highest-priority remaining encoder work in the same turn unless the user explicitly said to stop after that task.
- Preserve the same discipline, ownership rules, documentation habits, and validation rigor used in the completed H.264 RTL encoder project, but adapt them to AV1 syntax and workflow.
- Use the normal loop on every turn unless blocked:
  1. inspect current repo state
  2. implement the next highest-priority feature or fix
  3. build with maximum local parallelism
  4. run simulation and decode checks
  5. verify output and compare against expected behavior
  6. fix the next blocking failure
  7. continue to the next roadmap item
- After one item is working and verified, proceed directly to the next in-scope item from `av1-reference-docs/svt-av1-feature-inventory.md`.
- Do not ask for confirmation between normal coding, build, simulation, debug, or verification steps.
- Treat `av1-reference-docs/svt-av1-feature-inventory.md` as the live implementation backlog and keep moving to the next remaining item until the roadmap is complete.
- Keep `README.md` updated as a living project status document while implementation is in progress. Do not wait until the end of the project to refresh it.
- After each meaningful implementation or verification change, update `README.md` with the current supported subset, verification status, known gaps, and the current recommended run flow.
- After each meaningful implementation or correctness fix, update `AGENTS.md`, the relevant detailed status or inventory doc, create a focused git commit, push current progress, and continue working.
- Continuously update both `AGENTS.md` and `README.md` during active work. Do not wait only for milestones.
- Treat documentation updates as required implementation work whenever there is new information worth preserving.
- Update the docs after any of the following:
  - a feature is implemented or materially expanded
  - a verification result changes confidence or scope
  - a blocker is clarified, removed, or replaced by a narrower blocker
  - a root cause or important discovery is found
  - RTL ownership moves from software debug paths onto the raw RTL path
  - a roadmap item is re-scoped, deferred, or unlocked
  - a new external reference materially changes the implementation plan
- `README.md` must capture current status, supported behavior, verification state, blockers, and discoveries worth preserving.
- `AGENTS.md` must capture durable execution rules, current focus, active ownership boundary, and any new working rules discovered during implementation.
- After each verified milestone, create a git commit that captures the completed work and push it to the configured remote before continuing.
- Use non-interactive git commands only. Do not stop at the commit or push; continue directly to the next backlog item after the push succeeds.
- A successful commit, push, and doc refresh is explicitly not a valid reply point by itself if code, build, sim, or verification work can still continue safely in the same turn.
- Reaching a clean handoff such as "the next target is X" is not a valid reason to reply. If the next target is known and locally actionable, start it immediately.
- Do not emit a milestone summary reply just because:
  - a verified checkpoint was preserved in git
  - the next ownership target was identified
  - a fresh debug trace or reduced repro was captured
  - the repo is in a good state to continue
- If work can continue responsibly after a push, it must continue in the same turn.
- When a blocker, ambiguity, or spec mismatch appears, do not stop at local guesswork. Pull authoritative external references into `av1-reference-docs/external/`, record the relevant findings there, and keep implementing from those materials.
- Prefer official sources for external references: AOMedia AV1 spec/material, FFmpeg documentation/source, and other primary project docs directly tied to the tool being used.
- During any active syntax, entropy-coding, motion-vector, frame-header, tile, or decode mismatch blocker, search the web for the current official source material before making more local guesses.
- Download or mirror the exact official pages, source files, or PDFs that are relevant to the blocker into `av1-reference-docs/external/` so the repo keeps a local working set for the next debug step.
- Treat `av1-reference-docs/external/` as a required working area during active blockers, not an optional notes folder.
- Keep `av1-reference-docs/external/README.md` current with the downloaded source files, why they were pulled, and what they clarified for the active blocker.
- Keep `README.md` current with which official external references were needed for the current blocker and what they changed in the implementation/debug plan.
- Only stop to ask the user when required for:
  - missing credentials or private external resources
  - destructive actions outside normal build/test flow
  - ambiguous product decisions that materially change implementation scope
  - a blocker that cannot be resolved locally with the available code, tools, and permissions
- If blocked, record:
  - the exact blocker
  - the last verified working state
  - the next step to resume from once the blocker is removed

## Workflow Priorities
1. Keep the encoder end to end through the RTL-owned bitstream path.
2. Fix correctness before adding more scope.
3. Start with the smallest valid case that proves the current issue.
4. Scale to longer clips only after decode and visual checks pass on smaller cases.
5. Preserve or improve reproducibility of the build and verification flow.
6. Do not stop while major baseline feature gaps are still open.
7. After each meaningful implementation step, update docs, push progress, and continue unless fully blocked.

## Current Focus
- The reduced still-picture header and raw frame-size ownership checkpoints are already on the RTL byte path.
- The entropy foundation now includes reference-matching bool, literal, and generic CDF symbol coding in `rtl/av1_entropy.v`.
- The top-level now mirrors the writer's 8x8 neighborhood syntax context state and emits the real AV1 skip symbol, intra `y_mode`, zero `angle_delta`, deterministic `uv_mode`, reduced luma/chroma `txb_skip` entry symbols, and a bounded DC-only real coefficient slice on the raw RTL path for the small keyframe/intra subset.
- The raw RTL path now also owns the first verified sparse low-order AC subset on the exact-match `16x16` `data/ac_probe_16x16_1f.yuv` `qindex=240` probe:
  - only `qcoeff[0]` and `qcoeff[1]` are nonzero, `qcoeff[8] == 0`
  - real AV1 syntax on the RTL path now covers `eob_multi64` symbol `2`, `eob_extra=0`, the EOB coeff at scan `c=2`, the zero base at scan `c=1`, the DC base, and the DC/AC signs
- The raw RTL path now also owns the first verified dense low-order AC subset on that same exact-match probe:
  - only flat indices `0`, `8`, `1`, and `10` are nonzero with magnitudes `1`, `1`, `2`, and `1`
  - real AV1 syntax on the RTL path now covers `eob_multi64` symbol `4`, `eob_extra=0` including the direct bits, the intervening zero bases at scan positions `7..3`, the context-2 bases at scan `2` and `1`, and the full DC/AC sign chain
- The reduced software-owned path is now exact again on the first sparse AC still-picture probe:
  - `tb/av1_bitstream_writer.h` uses the official libaom `default_scan_8x8`
  - `rtl/av1_encoder_top.v` transposes the dequantized 8x8 coefficient matrix into decoder-consistent orientation before inverse transform
  - `rtl/av1_intra_pred.v` now matches the reference left-only / top-only edge fill rules for non-directional intra modes
- The focused ownership checkpoint now has a direct RTL-byte capture path in the testbench:
  - `tb/tb_av1_encoder.cpp` records `bs_byte_valid`, `ec_byte_valid`, and explicit `manual_bs_wr` back-patches directly from the RTL top-level mux when building `*_rtl_raw.obu` / `*_rtl.ivf`
  - on the current `16x16` ownership probe, those RTL-owned artifacts now match the software-owned payload byte-for-byte and decode cleanly in ffmpeg/libdav1d once wrapped in IVF
- The raw RTL path is now exact again across the current strict `16x16` q sweeps:
  - the natural DC-only `16x16` crop still matches byte-for-byte between software-owned and RTL-owned payloads from `qindex=1` through `qindex=240`
  - the local `data/tmp_probe_16x16_1f.yuv` fallback currently decodes to `recon.yuv` but is not byte-exact in this checkout; a temporary revert of the new inter-path change shows the same `34` vs `35` byte drift, so do not use that clip as the exact-match regression gate
  - representative low-q and high-q decodes on the natural DC-only crop still match `recon.yuv` after the DC-base fix
- The raw RTL path now advances block syntax in the same recursive partition-tree leaf order as the writer and decoder:
  - `rtl/av1_encoder_top.v` now steps `8x8` leaves in Morton order inside each `64x64` superblock instead of plain raster order
  - that cleared the first larger-frame ownership drift after the `16x16` exact-match fixes
- The current single-frame ownership checkpoints now extend beyond the original `16x16` probes:
  - the first natural-content `32x32` `qindex=128` crop is now byte-exact between `encoded.obu` and `encoded_rtl_raw.obu`, and both decoded outputs match `recon.yuv`
  - the first natural-content `64x64` `qindex=128` crop is now byte-exact between `encoded.obu` and `encoded_rtl_raw.obu`, and both decoded outputs match `recon.yuv`
- The video-keyframe debug path is also back in sync on that same exact-match probe:
  - `tb/av1_bitstream_writer.h` now emits the missing non-still `refresh_frame_context` bit before `tile_info`
  - the current sequence header still advertises `enable_intra_edge_filter = 0`, so the directional predictor must not apply directional edge upsampling on the RTL reconstruction path until that syntax bit is owned and enabled
- The non-key ownership path now has its first real RTL-owned block and header syntax moves:
  - `rtl/av1_encoder_top.v` now emits the real `intra_inter` symbol on non-key blocks before mode syntax
  - `rtl/av1_bitstream.v` now emits a reduced video `INTER_FRAME` header instead of the old placeholder non-key bytes
  - `tb/test_rtl_bitstream.cpp` plus `make bitstream-check` now lock the standalone sequence header, video keyframe header, and video inter-frame header bytes against a reduced reference model
- The current raw-path inter subset is intentionally narrowed while ownership grows:
  - `use_inter` is currently clamped to zero-motion matches on the RTL-owned path
  - the top-level now tracks reduced inter neighborhood state (`inter`, `ref`, reduced inter mode) so zero-motion `GLOBALMV` syntax can be extended toward `NEARESTMV` / `NEWMV`
  - do not widen the raw inter subset again until the missing reference signaling, inter-mode, and MV payload syntax are actually emitted and verified
- The smallest real multi-frame zero-motion ownership checkpoint is now exact:
  - on the strict `16x16` 2-frame flat repeated-frame IP repro, `encoded.obu` and `encoded_rtl_raw.obu` now match byte-for-byte
  - on that same repro, `encoded.ivf` and `encoded_rtl.ivf` now match byte-for-byte and decode back to both `recon.yuv` and source exactly
  - the last remaining frame-1 tile-data drift was the RTL reduced single-ref `cmp_ctx=2` / branch-0 CDF entry; it now matches the software-owned path at `3024`
- The first larger natural-content repeated-frame zero-motion ownership checkpoint is now exact:
  - on `data/natural_repeat64_x640_y360_2f.yuv` (`64x64`, 2 frames, repeated frame-0 crop at `(640,360)`, `qindex=128`), `encoded.obu` and `encoded_rtl_raw.obu` now match byte-for-byte
  - on that same clip, `encoded.ivf` and `encoded_rtl.ivf` now match byte-for-byte and the decoded RTL IVF matches `recon.yuv`
  - the last drift there was zero-motion inter blocks still taking the placeholder coefficient path and the intra `tx_type` CDF; `rtl/av1_encoder_top.v` now routes those blocks through the real generic coefficient path and the inter `DCT_DCT` CDF
- Directional intra availability for the current fixed `8x8` / `TX_8X8` raster-order subset is now partially corrected:
  - real top-right extension samples are loaded and used when the above-right block is already reconstructed
  - bottom-left extension remains intentionally disabled on this subset because it would otherwise read future not-yet-reconstructed pixels and corrupt exactness
  - on the rebuilt live tree, the old `qindex=224` residual no longer reproduces on the verified `qindex=240` probe
  - keep directional edge upsampling disabled while `enable_intra_edge_filter = 0`; re-enable it only when the bitstream path owns and signals that sequence-header feature correctly
- The next highest-priority ownership move is widening the reduced inter syntax beyond zero-motion `GLOBALMV` now that the first larger natural-content repeated-frame guard is exact:
  - keep the `16x16` `data/ac_probe_16x16_1f.yuv` exact-match case as the first regression gate when that asset is available in the checkout
  - do not substitute `data/tmp_probe_16x16_1f.yuv` for byte-exact ownership checks; it is currently decode-clean but not exact
  - keep the new `32x32` and `64x64` `qindex=128` Big Buck Bunny crops plus `data/natural_repeat64_x640_y360_2f.yuv` as the partition-order and larger-frame regression guards
  - keep `make bitstream-check WIDTH=16 HEIGHT=16` in the normal quick regression loop whenever `rtl/av1_bitstream.v` changes
  - do not spend more time on the old `qindex=224` blocker unless it reappears after a real code change
  - use `output/highdc_q1/` as the strict large-DC regression guard and `data/ac_probe_16x16_1f.yuv` at `qindex=240` as the verified exact-match regression guard
  - then continue pulling the remaining reduced inter syntax onto the RTL byte path in this order:
    - `NEARESTMV` / `NEWMV` mode signaling
    - MV payload syntax
    - longer multi-frame decode verification
- The immediate correctness target after the raw-byte mux fix is the reference-decoder-backed syntax split:
  - the strict non-lossless `output/highdc_q1/` bug is fixed:
    - the software debug writer and the RTL-owned raw path now use official qctx-selected TX_8X8 coefficient tables instead of the old hardcoded `qctx=3` slice
    - AOM inspection now parses the intended `qindex=1` large-DC blocks as `tx_size=1`, `eob=1`, with the expected Golomb tail
    - ffmpeg decode now matches `recon.yuv` bit-for-bit on that repro
  - `qindex=0` remains a separate deferred lossless / `TX_4X4` feature:
    - AOM inspection shows the decoder taking the lossless `TX_4X4` path when `base_q_idx=0`
    - until that path is implemented, requested `qindex=0` runs clamp to effective `qindex=1` in both the testbench and RTL top-level so the supported subset stays valid
- Do not treat the entropy-core milestone as completion. It only removes one foundation blocker for the tile/payload ownership work.

## Stop Conditions
- Milestones are not stop conditions.
- Progress updates are not stop conditions.
- Partial verification is not a stop condition.
- A clean checkpoint is not a stop condition.
- A completed side task or maintenance request is not a stop condition.
- The end of a turn is not a stop condition by itself.
- A convenient place to summarize is not a stop condition.
- "This is a good place to stop" is not a valid reason to stop work.
- A successful build, decode, or single feature bring-up is not a stop condition by itself.
- A successful commit, push, or captured next-step plan is not a stop condition.
- A clean repo state after a verified milestone is not a stop condition.
- Finishing `.gitignore`, docs, scripts, cleanup, or other repo-maintenance work is not a valid reason to stop if roadmap work still remains.
- After any milestone, commit and push the verified work, then continue immediately to the next highest-priority remaining task.
- Only stop and reply when:
  - the full acceptance criteria are complete, or
  - a hard blocker is reached that cannot be resolved locally with the available code, files, tools, network access, and permissions, or
  - continuing without stopping would be materially detrimental to the project
    - examples: destructive-risk changes without confirmation, unverifiable guesswork that would likely corrupt the ownership path, or resource/time usage that would clearly damage reproducibility without producing useful signal

## Reporting Policy
- Do not reply just because a milestone was reached.
- Do not pause for progress summaries or routine check-ins.
- If a milestone is reached, commit it, push it, and keep working without waiting for confirmation.
- Keep `README.md` and `AGENTS.md` current instead of using milestone replies as the main status log.
- Do not reply only because a side-task request was completed if active encoder implementation work still remains.
- Do not reply only because the work reached a natural pause point.
- Do not reply only because a push or doc update just completed.
- Do not reply only because the next target is now known or because a useful debug trace was captured.
- Do not reply with "what changed + next target" if the next target can already be worked on locally.
- After documentation updates, side-task completions, milestone commits, pushes, or verification wins, continue directly into the next highest-priority task.
- If a hard blocker is reached, report only:
  - the exact blocker
  - the last verified working state
  - the immediate next step once unblocked

## Documentation Discipline
- Documentation drift is not allowed.
- When progress changes what is true about the project, update the docs in the same work cycle.
- Do not leave important discoveries only in commit messages, terminal logs, or assistant replies.
- If a discovery is important enough to affect the next implementation step, it is important enough to record in `README.md` and, when it changes agent behavior, in `AGENTS.md`.
- `README.md` is the living engineering status file.
- `AGENTS.md` is the living execution-policy and current-focus file.

## Git Policy
- Treat each verified milestone as a required git sync point.
- After a milestone is verified, update `README.md`, create a focused commit, and push it to the configured remote branch.
- Prefer small milestone commits over large batch commits.
- Do not use interactive git workflows.
- If `git push` fails because of credentials, permissions, remote policy, or network issues, record the exact failure as the blocker and resume pushing as soon as the blocker is removed.

## Prior Work
The H.264 RTL encoder was already completed and is located at `av1-reference-docs/h264-rtl-encoder/`. Follow the same file structure and rules established in that project for this AV1 encoder. Mirror its folder layout for the encoder and docs.

## Reference Material
- **SVT-AV1 reference repo**: `../SVT-AV1/` - This repo is reference-only. Always consult it instead of guessing. If you are unsure how something works in AV1, look it up here first.
- **SVT-AV1 feature inventory**: `av1-reference-docs/svt-av1-feature-inventory.md` - Use this as the implementation roadmap and feature triage document before adding new encoder functionality.
- **AV1 specification**: `av1-reference-docs/av1-spec.pdf` - The official spec sheet is available for detailed reference.
- **External official references**: `av1-reference-docs/external/README.md` - Keep downloaded official web docs and source references here when the active blocker needs more than the local spec snapshot and SVT code.
- **AV1 reference software**: use local or mirrored `libaom` source as the practical bitstream and entropy-coding reference.
- **Software baseline**: use `SVT-AV1` and/or `libaom` reference encodes to sanity-check behavior against a mature software encoder.
- **Do NOT guess.** If you need help understanding AV1 encoding stages, syntax elements, transforms, or anything else, reference the SVT-AV1 source code, the feature inventory, and the spec. Never fabricate implementation details.
- Always prefer primary references over memory. Download and keep needed spec sheets and reference material available locally.
- If an inference is unavoidable, mark it as temporary and verify it as soon as possible against the spec or reference software.

## Test Video
- **Big Buck Bunny** at **720p** is the reference video for all testing and verification.
- **Final deliverable**: First **10 seconds** of Big Buck Bunny at **1280x720 @ 24 fps**, encoded by the RTL AV1 encoder, decoded, visually verified, and delivered as a playable MP4 derived from the RTL-generated AV1 stream.
- During initial development and verification, you do not need to encode all 10 seconds - use smaller clips or single frames to validate. The full 10-second encode is the final project milestone.

## Simulation Environment
- Prefer Linux flow via **WSL Ubuntu** or **Docker Ubuntu**.
- Docker configuration and simulation scripts go in the Docker Ubuntu folder.
- For Verilator builds and simulation, use the maximum available host threads by default unless a specific debugging task requires fewer threads.
- On this machine, the default target is `24` threads and `24` build jobs.
- If the environment does not reliably expose all CPUs through `nproc`, explicitly set `THREADS=24` and `BUILD_JOBS=24`.
- Do not quietly fall back to reduced parallelism for normal runs.
- Be wary of simulation times. Do not jump to long or high-resolution runs until smaller cases decode and look sane.
- After RTL edits, prefer clean rebuilds so stale simulator outputs do not mask changes.

## Encoding Configuration
- **Chroma subsampling**: 4:2:0
- **Bit depth**: 8-bit (for now)
- **Resolution**: Variable, but **must be a multiple of 16** in both width and height.
- **Frame types**: I-frames and P-frames only (no B-frames).

## Feature Scope - Required Implementation Order
- Implement features in the phased order defined in `av1-reference-docs/svt-av1-feature-inventory.md`.
- Do **not** jump to advanced AV1 tools before the lower phase is working, decoded, and visually verified.
- The first working encoder target is a **low-delay real-time subset**, not a full SVT-AV1 clone.
- That low-delay subset is only the first milestone. The end goal remains implementation of the full proposed AV1 feature set recorded in the inventory document.

### P0 - First Bring-Up
- **Prediction structure**: Low-delay only.
- **Refresh structure**: Closed GOP keyframes only.
- **Frames**: I and P frames only.
- **Resolution / format**: 8-bit 4:2:0 only, dimensions multiple of 16.
- **Partitioning**: Start with **64x64 superblocks** and **square partitions only**.
- **Intra prediction**: Implement **DC**, **directional angular**, and **Paeth** first.
- **Inter prediction**: Implement **single-reference translational inter** first.
- **Motion estimation**: Implement **integer-pel** and **fractional-pel** translational ME only.
- **Transform / quantization**: Start with a **reduced transform set** sufficient for conformant bitstream bring-up.
- **Bitstream path**: Implement **quantization**, **entropy coding**, and **packetization** as part of the first milestone.
- **Rate control**: Start with **fixed QP**.

### P1 - First Real-Time Usable Encoder
- Improve partition search quality.
- Expand intra mode coverage beyond the P0 subset.
- Improve MV predictor reuse.
- Add **simple CBR**.
- Add **basic deblocking filter**.

### P2 - Quality Features Still Compatible With Real-Time
- Add **AQ / SB-level QP modulation**.
- Add **limited lookahead** only if needed.
- Add **CDEF**.
- Add more transform choices.
- Add limited non-square partitions if justified by quality.
- Add tiles only if throughput requires them.

### P3 - Advanced / Deferred Features
- Random access prediction structures.
- Deep temporal hierarchy.
- ALT-REF and overlay workflows.
- Super-resolution and reference scaling / resize.
- Restoration filter.
- Multi-pass rate control.
- Film grain synthesis.
- Screen-content tools such as IBC and palette.
- Complex inter tools such as OBMC, warped motion, global motion, wedge, and advanced compound modes.

## Implementation Guardrails
- Do not implement random-access, reordering-heavy, or VOD-oriented tools before the low-delay baseline is complete.
- Do not implement screen-content tools unless the target workload explicitly requires desktop, UI, remote-desktop, or game-streaming content.
- Do not implement superres, resize, overlays, or multi-pass logic during the first hardware bring-up.
- Metadata signaling is allowed early if it does not destabilize the core encode pipeline.
- If a feature is not covered by the current phase, document it and defer it instead of partially guessing an implementation.

## Verification - NON-NEGOTIABLE
- The RTL-generated stream must decode successfully in FFmpeg or another standards-compliant AV1 decoder.
- You must **visually confirm** that the encoder works.
- The **decoded output must look like the input** to verify that encoding and decoding are functioning correctly.
- Use **ffmpeg** to compare input vs output (e.g., PSNR/SSIM metrics, frame extraction for visual diff).
- **Always compare your RTL encoder output against ffmpeg's own AV1 encode** of the same source to sanity-check correctness. If your output diverges significantly from what ffmpeg produces, something is wrong - investigate before moving on.
- Preserve simulator logs and cycle counts for repeatable validation runs.
- Keep the decoded output on a valid path toward the final RTL-generated bitstream requirement.
- **Do not stop until this is confirmed.** The encoder is not done until the RTL path has encoded, decoded, and been visually verified against the original.

## RTL Ownership Requirement
- The RTL must own the final AV1 syntax generation needed for project completion, including as appropriate to the implemented stage:
  - sequence header / sequence header OBU generation
  - frame header / frame OBU generation
  - tile group syntax generation
  - entropy coding on the RTL path
  - partition syntax
  - intra/inter mode syntax
  - motion syntax
  - transform syntax
  - coefficient syntax
  - trailing, alignment, and packing rules required for a valid AV1 stream
- The testbench may:
  - feed raw YUV input
  - capture RTL byte output
  - decode output
  - package the bitstream into a playable container
  - compute PSNR / SSIM
  - generate side-by-side comparisons
- The testbench must not:
  - build the final AV1 syntax in software
  - replace the RTL bitstream writer with a helper-generated final stream

## Acceptance Criteria
- Do not treat the project as complete until all in-scope planned features from `av1-reference-docs/svt-av1-feature-inventory.md` are implemented or explicitly re-scoped in this file.
- Completion requires more than code written. It requires:
  - the AV1 stream is produced by the RTL byte path
  - supported AV1 syntax is implemented in RTL and emitted in a decodable bitstream
  - decoded output matches intended RTL reconstruction for supported tools and modes
  - the stream is visually verified against the source
  - intra and inter paths are both verified on project test content
  - comparison against `ffmpeg` AV1 reference output for sanity checking
  - normal regression/build flow is run on this machine with `THREADS=24` and `BUILD_JOBS=24`
  - the final Big Buck Bunny deliverable is produced at `1280x720 @ 24 fps` according to the project rules
- Finishing one phase does not count as overall completion. Move to the next phase automatically until all required phases are complete or a concrete local blocker is reached.

## Agent Rules
- When using sub-agents, use **Opus MAX 4.6** only.
- Treat `av1-reference-docs/svt-av1-feature-inventory.md` as the authoritative implementation backlog for AV1 encoder features.
- Continue implementing features until the encoder reaches full planned feature coverage and verification, not just the first successful frame.
- Do not declare completion while proposed roadmap features remain unimplemented unless the project rules are explicitly changed.
- If blocked, record the blocker concretely and resume from the highest-priority remaining feature as soon as the blocker is removed.
- If the repo already contains partial AV1 RTL, first inventory it and classify it as:
  - implemented
  - validated
  - missing
  - broken
  - placeholder or debug-only
  - not RTL-owned yet
- Then close the highest-leverage correctness and ownership gaps first.

## Workflow
1. Reference the H.264 encoder repo for file structure and conventions.
2. Reference `av1-reference-docs/svt-av1-feature-inventory.md` to determine the current feature phase and what is explicitly deferred.
3. Reference SVT-AV1 and the AV1 spec for all AV1-specific implementation details.
4. Implement only the current phase feature subset unless the project rules are explicitly expanded.
5. Build and simulate RTL in the Docker Ubuntu environment.
6. Encode Big Buck Bunny frames, decode them, and visually compare output to input.
7. Advance to the next phase after the current phase is implemented, decoded, and verified.
8. Only mark the encoder as complete once the full proposed feature roadmap has been implemented and verification passes.
