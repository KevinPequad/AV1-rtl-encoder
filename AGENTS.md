# AV1 RTL Encoder - Project Rules

## Prior Work
The H.264 RTL encoder was already completed and is located at `av1-reference-docs/h264-rtl-encoder/`. Follow the same file structure and rules established in that project for this AV1 encoder. Mirror its folder layout for the encoder and docs.

## Reference Material
- **SVT-AV1 reference repo**: `av1-reference-docs/SVT-AV1/` — Always consult this instead of guessing. If you are unsure how something works in AV1, look it up here first.
- **AV1 specification**: `av1-reference-docs/av1-spec.pdf` — The official spec sheet is available for detailed reference.
- **Do NOT guess.** If you need help understanding AV1 encoding stages, syntax elements, transforms, or anything else — reference the SVT-AV1 source code and the spec. Never fabricate implementation details.

## Test Video
- **Big Buck Bunny** at **720p** is the reference video for all testing and verification.
- **Final deliverable**: First **10 seconds** of Big Buck Bunny, encoded by our RTL AV1 encoder, decoded, and output as an MP4.
- During initial development and verification, you don't need to encode all 10 seconds — use smaller clips or single frames to validate. The full 10-second encode is the final project milestone.

## Simulation Environment
- All RTL simulation must be run inside a **Docker Ubuntu** container.
- Docker configuration and simulation scripts go in the Docker Ubuntu folder.
- For Verilator builds and simulation, use the maximum available host threads by default unless a specific debugging task requires fewer threads. On this machine, that means `24` threads.

## Encoding Configuration
- **Chroma subsampling**: 4:2:0
- **Bit depth**: 8-bit (for now)
- **Resolution**: Variable, but **must be a multiple of 16** in both width and height.
- **Frame types**: I-frames and P-frames only (no B-frames).

## Verification — NON-NEGOTIABLE
- You must **visually confirm** that the encoder works.
- The **decoded output must look like the input** to verify that encoding and decoding are functioning correctly.
- Use **ffmpeg** to compare input vs output (e.g., PSNR/SSIM metrics, frame extraction for visual diff).
- **Always compare your RTL encoder output against ffmpeg's own AV1 encode** of the same source to sanity-check correctness. If your output diverges significantly from what ffmpeg produces, something is wrong — investigate before moving on.
- **Do not stop until this is confirmed.** The encoder is not done until a frame has been encoded, decoded, and visually verified against the original.

## Agent Rules
- When using sub-agents, use **Opus MAX 4.6** only.

## Workflow
1. Reference the H.264 encoder repo for file structure and conventions.
2. Reference SVT-AV1 and the AV1 spec for all AV1-specific implementation details.
3. Build and simulate RTL in the Docker Ubuntu environment.
4. Encode Big Buck Bunny frames, decode them, and visually compare output to input.
5. Only mark the encoder as complete once visual verification passes.
