#!/bin/bash
set -euo pipefail

if [ "$#" -ne 6 ]; then
    echo "Usage: $0 <frame_dir> <decoded_yuv> <output_mp4> <width> <height> <fps>" >&2
    exit 1
fi

FRAME_DIR="$1"
DECODED_YUV="$2"
OUTPUT_MP4="$3"
WIDTH="$4"
HEIGHT="$5"
FPS="$6"

shopt -s nullglob
FRAME_FILES=("$FRAME_DIR"/frame_*.ivf)

if [ "${#FRAME_FILES[@]}" -eq 0 ]; then
    echo "ERROR: no frame IVF files found in $FRAME_DIR" >&2
    exit 1
fi

mkdir -p "$(dirname "$DECODED_YUV")"
mkdir -p "$(dirname "$OUTPUT_MP4")"
rm -f "$DECODED_YUV" "$OUTPUT_MP4"

echo "Decoding ${#FRAME_FILES[@]} AV1 still frames from $FRAME_DIR ..."
for ivf in "${FRAME_FILES[@]}"; do
    ffmpeg -y -hide_banner -loglevel error \
        -i "$ivf" \
        -f rawvideo \
        -pix_fmt yuv420p \
        - >> "$DECODED_YUV"
done

echo "Packaging decoded sequence to MP4 ..."
ffmpeg -y -hide_banner -loglevel error \
    -f rawvideo \
    -pix_fmt yuv420p \
    -s "${WIDTH}x${HEIGHT}" \
    -r "$FPS" \
    -i "$DECODED_YUV" \
    -c:v libx264 \
    -pix_fmt yuv420p \
    -movflags +faststart \
    "$OUTPUT_MP4"

echo "Decoded YUV: $DECODED_YUV"
echo "Output MP4:  $OUTPUT_MP4"
