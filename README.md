# AV1 RTL Encoder

This repository is the main implementation tree for the AV1 RTL encoder. The sibling `../SVT-AV1/` repository is reference-only and is used to validate feature behavior against a mature software encoder.

## Goal

The project goal is to implement the full AV1 encoder feature roadmap captured in `av1-reference-docs/svt-av1-feature-inventory.md`, starting with the real-time low-delay subset and continuing phase by phase until the encoder is feature-complete and verified.

## Living Document

This `README.md` is a living status document and must be updated continuously while the encoder is being implemented.
Official external debug references pulled from the web are tracked in `av1-reference-docs/external/README.md`.

- Do not wait until project completion to refresh this file.
- Update it after meaningful implementation, simulation, decode, or verification changes.
- Keep it aligned with `AGENTS.md` and `av1-reference-docs/svt-av1-feature-inventory.md`.
- It must reflect the current supported feature subset, current verification status, known gaps, active blockers, and the currently recommended run flow.

## Simulation Threads

Simulation and Verilator builds must use all available host threads by default.

- On this machine, the expected host thread count is `24`.
- Leave `THREADS` unset only when the runtime environment correctly exposes all host CPUs via `nproc`.
- When in doubt, force both `THREADS=24` and `BUILD_JOBS=24`.

Current build scripts already default to maximum detected threads:

- `tb/Makefile` uses `THREADS ?= $(shell nproc 2>/dev/null || echo 24)`
- `run.sh` uses `THREADS=${THREADS:-$(nproc)}`
- `build_run.sh` uses `THREADS=${THREADS:-$(nproc)}`

Recommended commands on this machine:

```bash
THREADS=24 BUILD_JOBS=24 bash docker_run.sh
```

```bash
THREADS=24 BUILD_JOBS=24 bash run.sh
```

```bash
cd tb
make THREADS=24 BUILD_JOBS=24
```

## Verification Inputs

The repo-local test assets are stored under `data/`:

- `bigbuckbunny.mp4`
- `raw_frames.yuv`
- `ffmpeg_reference.ivf`

These are used for RTL encode, decode, and comparison against an ffmpeg AV1 reference encode.

## Current Status

- Verified all-key `64x64` end-to-end encode, decode, and RTL-reconstruction match with `THREADS=24` and `BUILD_JOBS=24`.
- The current verified path is:
  - 8-bit 4:2:0
  - fixed `8x8` luma blocks inside `64x64` superblocks
  - luma intra mode search across `DC`, directional, `SMOOTH`, and `PAETH`
  - real luma transform, quantization, coefficient coding, inverse transform, and reconstruction
  - decodable still-picture IVF output
  - decodable all-key IVF sequence output
- The software writer and RTL reconstruction are currently aligned for chroma by using flat `128` chroma reconstruction until real chroma residual coding is implemented.
- A minimal non-key writer path now exists for:
  - a video-sequence bootstrap frame using `INTRA_ONLY` syntax to seed a valid reference state without falling back to still-picture syntax
  - exact decode-equals-RTL-reconstruction verification for the mixed-sequence `+force_intra=1` path on a `64x64` 2-frame test
  - intra blocks inside inter frames
  - single-reference `LAST_FRAME`
  - zero-motion `GLOBALMV` inter blocks
- The RTL is currently gated to that same zero-motion inter subset so unsupported non-zero-MV blocks do not enter the writer path accidentally.
- Official AOMedia and FFmpeg references for active syntax/debug blockers are stored under `av1-reference-docs/external/`.
- The non-key forced-intra path is now verified decode-equals-recon, but true inter-frame AV1 block syntax is still only partially implemented and not yet verified decode-equals-recon.

## Known Gaps

- Full P-frame/inter-frame bitstream serialization is still incomplete.
- The current inter writer only targets the zero-motion subset. `NEWMV` / non-zero-MV block coding is still missing.
- The current blocker is the true inter-frame sequence path: the mixed-sequence bootstrap plus forced-intra path is exact, but switching frame 1 to inter coding still produces an invalid sequence.
- Real chroma residual coding is still missing. The current path prioritizes decoded-output-equals-RTL-reconstruction verification over chroma fidelity.
- The current fully verified decode-equals-recon checks are:
  - the all-key subset
  - the mixed-sequence bootstrap plus forced-intra subset
- True inter-coded sequences are still not verified.

## Recommended Run Flow

Use the repo scripts for broad runs, and use a small `64x64` focused check when validating bitstream/reconstruction changes quickly.

Broad run:

```bash
THREADS=24 BUILD_JOBS=24 bash run.sh
```

Focused verification flow:

```bash
cd tb
make THREADS=24 BUILD_JOBS=24 WIDTH=64 HEIGHT=64
./Vav1_encoder_top +frames=1 +qindex=128 +dc_only=0 +input=../data/raw_frames.yuv +output=../output/encoded.obu
```

For exact reconstruction checks, decode the generated IVF and compare it against `output/recon.yuv`.

For inter bring-up, use a repeated-frame `64x64` clip first so the zero-motion subset is exercised before attempting wider-motion content.
If a syntax blocker appears, check `av1-reference-docs/external/README.md` first and refresh that folder from official sources before guessing.
