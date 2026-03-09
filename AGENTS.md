# AV1 RTL Encoder - Project Rules

## Mission
- The mission of this repo is to implement the complete AV1 encoder feature roadmap defined in `av1-reference-docs/svt-av1-feature-inventory.md`.
- Work through the roadmap phase by phase until the encoder is feature-complete, decoded successfully, and visually verified.
- Do not stop at partial bring-up, partial syntax support, or documentation-only progress if there is still an implementation path available.
- Only stop early when blocked by a concrete external limitation such as a missing tool, missing dependency, or irreconcilable ambiguity, and document the exact blocker before stopping.

## Execution Policy
- Default behavior: continue work autonomously across implementation, build, simulation, debug, verification, and the next backlog item without waiting for confirmation between routine substeps.
- Do not stop at analysis, plans, documentation, or a single successful build if code changes and verification work are still possible.
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
- Keep `README.md` updated as a living project status document while implementation is in progress. Do not wait until the end of the project to refresh it.
- After each meaningful implementation or verification change, update `README.md` with the current supported subset, verification status, known gaps, and the current recommended run flow.
- After each verified milestone, create a git commit that captures the completed work and push it to the configured remote before continuing.
- Use non-interactive git commands only. Do not stop at the commit or push; continue directly to the next backlog item after the push succeeds.
- When a blocker, ambiguity, or spec mismatch appears, do not stop at local guesswork. Pull authoritative external references into `av1-reference-docs/external/`, record the relevant findings there, and keep implementing from those materials.
- Prefer official sources for external references: AOMedia AV1 spec/material, FFmpeg documentation/source, and other primary project docs directly tied to the tool being used.
- Keep `av1-reference-docs/external/README.md` current with the downloaded source files, why they were pulled, and what they clarified for the active blocker.
- Only stop to ask the user when required for:
  - missing credentials or private external resources
  - destructive actions outside normal build/test flow
  - ambiguous product decisions that materially change implementation scope
  - a blocker that cannot be resolved locally with the available code, tools, and permissions
- If blocked, record:
  - the exact blocker
  - the last verified working state
  - the next step to resume from once the blocker is removed

## Stop Conditions
- Milestones are not stop conditions.
- Partial verification is not a stop condition.
- A clean checkpoint is not a stop condition.
- A successful build, decode, or single feature bring-up is not a stop condition by itself.
- After any milestone, commit and push the verified work, then continue immediately to the next highest-priority remaining task.
- Only stop and reply when:
  - the full acceptance criteria are complete, or
  - a hard blocker is reached that cannot be resolved locally with the available code, files, tools, network access, and permissions

## Reporting Policy
- Do not reply just because a milestone was reached.
- Do not pause for progress summaries or routine check-ins.
- If a milestone is reached, commit it, push it, and keep working without waiting for confirmation.
- Keep `README.md` current instead of using milestone replies as the main status log.
- If a hard blocker is reached, report only:
  - the exact blocker
  - the last verified working state
  - the immediate next step once unblocked

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
- **Do NOT guess.** If you need help understanding AV1 encoding stages, syntax elements, transforms, or anything else, reference the SVT-AV1 source code, the feature inventory, and the spec. Never fabricate implementation details.

## Test Video
- **Big Buck Bunny** at **720p** is the reference video for all testing and verification.
- **Final deliverable**: First **10 seconds** of Big Buck Bunny, encoded by our RTL AV1 encoder, decoded, and output as an MP4.
- During initial development and verification, you do not need to encode all 10 seconds - use smaller clips or single frames to validate. The full 10-second encode is the final project milestone.

## Simulation Environment
- All RTL simulation must be run inside a **Docker Ubuntu** container.
- Docker configuration and simulation scripts go in the Docker Ubuntu folder.
- For Verilator builds and simulation, use the maximum available host threads by default unless a specific debugging task requires fewer threads.
- On this machine, the default target is `24` threads and `24` build jobs.
- If the environment does not reliably expose all CPUs through `nproc`, explicitly set `THREADS=24` and `BUILD_JOBS=24`.
- Do not quietly fall back to reduced parallelism for normal runs.

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
- You must **visually confirm** that the encoder works.
- The **decoded output must look like the input** to verify that encoding and decoding are functioning correctly.
- Use **ffmpeg** to compare input vs output (e.g., PSNR/SSIM metrics, frame extraction for visual diff).
- **Always compare your RTL encoder output against ffmpeg's own AV1 encode** of the same source to sanity-check correctness. If your output diverges significantly from what ffmpeg produces, something is wrong - investigate before moving on.
- **Do not stop until this is confirmed.** The encoder is not done until a frame has been encoded, decoded, and visually verified against the original.

## Acceptance Criteria
- Do not treat the project as complete until all in-scope planned features from `av1-reference-docs/svt-av1-feature-inventory.md` are implemented or explicitly re-scoped in this file.
- Completion requires more than code written. It requires:
  - supported AV1 syntax implemented in RTL and emitted in a decodable bitstream
  - decoded output matching intended RTL reconstruction for supported tools and modes
  - intra and inter paths both verified on project test content
  - comparison against `ffmpeg` AV1 reference output for sanity checking
  - normal regression/build flow run on this machine with `THREADS=24` and `BUILD_JOBS=24`
  - final Big Buck Bunny deliverable produced according to the project rules
- Finishing one phase does not count as overall completion. Move to the next phase automatically until all required phases are complete or a concrete local blocker is reached.

## Agent Rules
- When using sub-agents, use **Opus MAX 4.6** only.
- Treat `av1-reference-docs/svt-av1-feature-inventory.md` as the authoritative implementation backlog for AV1 encoder features.
- Continue implementing features until the encoder reaches full planned feature coverage and verification, not just the first successful frame.
- Do not declare completion while proposed roadmap features remain unimplemented unless the project rules are explicitly changed.
- If blocked, record the blocker concretely and resume from the highest-priority remaining feature as soon as the blocker is removed.

## Workflow
1. Reference the H.264 encoder repo for file structure and conventions.
2. Reference `av1-reference-docs/svt-av1-feature-inventory.md` to determine the current feature phase and what is explicitly deferred.
3. Reference SVT-AV1 and the AV1 spec for all AV1-specific implementation details.
4. Implement only the current phase feature subset unless the project rules are explicitly expanded.
5. Build and simulate RTL in the Docker Ubuntu environment.
6. Encode Big Buck Bunny frames, decode them, and visually compare output to input.
7. Advance to the next phase after the current phase is implemented, decoded, and verified.
8. Only mark the encoder as complete once the full proposed feature roadmap has been implemented and verification passes.
