# AV1 RTL Encoder

This repository is the main implementation tree for the AV1 RTL encoder. The sibling `../SVT-AV1/` repository is reference-only and is used to validate feature behavior against a mature software encoder.

## Goal

The project goal is to implement the full AV1 encoder feature roadmap captured in `av1-reference-docs/svt-av1-feature-inventory.md`, starting with the real-time low-delay subset and continuing phase by phase until the encoder is feature-complete and verified.

## Living Document

This `README.md` is a living status document and must be updated continuously while the encoder is being implemented.

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
- The non-key forced-intra path is present, but true inter-frame AV1 block syntax is not finished yet.

## Known Gaps

- True P-frame/inter-frame bitstream serialization is still incomplete.
- The RTL can select inter blocks and produce MVs, but the software AV1 writer does not yet encode real inter block syntax from that state.
- Real chroma residual coding is still missing. The current path prioritizes decoded-output-equals-RTL-reconstruction verification over chroma fidelity.
- The current fully verified decode-equals-recon check is for the all-key subset, not for true inter-coded sequences.

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
