#!/bin/bash
set -e

VIDEO_URL="${VIDEO_URL:-https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4}"
OUTPUT_DIR="${OUTPUT_DIR:-/workspace/data}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FPS="${FPS:-24}"
CLIP_SECONDS="${CLIP_SECONDS:-10}"
mkdir -p "$OUTPUT_DIR"

echo "Downloading Big Buck Bunny..."
if ! wget -q -O "$OUTPUT_DIR/bigbuckbunny.mp4" "$VIDEO_URL"; then
    echo "ERROR: Failed to download video" >&2
    exit 1
fi

if [ ! -s "$OUTPUT_DIR/bigbuckbunny.mp4" ]; then
    echo "ERROR: Downloaded file is empty" >&2
    exit 1
fi

echo "Decoding first ${CLIP_SECONDS} seconds to raw YUV420 at ${WIDTH}x${HEIGHT}..."
ffmpeg -y -i "$OUTPUT_DIR/bigbuckbunny.mp4" \
    -t "$CLIP_SECONDS" \
    -vf "fps=${FPS},scale=${WIDTH}:${HEIGHT}" \
    -pix_fmt yuv420p \
    -f rawvideo \
    "$OUTPUT_DIR/raw_frames.yuv"

if [ ! -s "$OUTPUT_DIR/raw_frames.yuv" ]; then
    echo "ERROR: Failed to produce raw_frames.yuv" >&2
    exit 1
fi

FRAME_BYTES=$((WIDTH * HEIGHT * 3 / 2))
FILE_SIZE=$(stat -c%s "$OUTPUT_DIR/raw_frames.yuv" 2>/dev/null || stat -f%z "$OUTPUT_DIR/raw_frames.yuv")
FRAME_COUNT=$((FILE_SIZE / FRAME_BYTES))

echo "Extracted $FRAME_COUNT frames"
echo "Frame size: ${WIDTH}x${HEIGHT} YUV420 = $FRAME_BYTES bytes per frame"
echo "Total raw size: $FILE_SIZE bytes"

# Also generate ffmpeg reference AV1 encode for comparison
echo "Generating ffmpeg AV1 reference encode..."
ffmpeg -y -f rawvideo -pix_fmt yuv420p -s ${WIDTH}x${HEIGHT} -r ${FPS} \
    -i "$OUTPUT_DIR/raw_frames.yuv" \
    -c:v libaom-av1 -crf 30 -cpu-used 8 \
    "$OUTPUT_DIR/ffmpeg_reference.ivf"

echo "ffmpeg reference encode done: $OUTPUT_DIR/ffmpeg_reference.ivf"
