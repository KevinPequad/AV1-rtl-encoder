#!/bin/bash
set -e
BUILD=/tmp/av1_build
WIDTH=${WIDTH:-1280}
HEIGHT=${HEIGHT:-720}
FPS=${FPS:-24}
THREADS=${THREADS:-$(nproc)}
BUILD_JOBS=${BUILD_JOBS:-$THREADS}
CPU_LIST=${CPU_LIST:-0-$((THREADS - 1))}
FFMPEG_BIN=${FFMPEG_BIN:-}
if [ -z "$FFMPEG_BIN" ]; then
    if command -v ffmpeg >/dev/null 2>&1; then
        FFMPEG_BIN="$(command -v ffmpeg)"
    elif [ -x "$PWD/tools/ffmpeg/ffmpeg-7.0.2-amd64-static/ffmpeg" ]; then
        FFMPEG_BIN="$PWD/tools/ffmpeg/ffmpeg-7.0.2-amd64-static/ffmpeg"
    else
        echo "ERROR: ffmpeg not found" >&2
        exit 1
    fi
fi
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp -r "$PWD/rtl" "$BUILD/"
cp -r "$PWD/tb" "$BUILD/"
cp -r "$PWD/data" "$BUILD/" 2>/dev/null || mkdir -p "$BUILD/data"
mkdir -p "$BUILD/output"

cd "$BUILD/tb"
make clean && make THREADS=$THREADS BUILD_JOBS=$BUILD_JOBS
echo "=== Build OK ==="

FRAMES=${1:-1}
QINDEX=${2:-128}
TIMEOUT=${3:-500000000}
DC_ONLY=${DC_ONLY:-1}
ENCODED_IVF="$BUILD/output/encoded.ivf"
SIM_CMD=(./Vav1_encoder_top +frames=$FRAMES +qindex=$QINDEX \
    +input=$BUILD/data/raw_frames.yuv \
    +output=$BUILD/output/encoded.obu +timeout=$TIMEOUT +dc_only=$DC_ONLY)
if command -v taskset >/dev/null 2>&1; then
    taskset -c "$CPU_LIST" "${SIM_CMD[@]}" 2>&1
else
    "${SIM_CMD[@]}" 2>&1
fi

cp "$BUILD/output/encoded.obu" "$PWD/output/encoded.obu" 2>/dev/null || true
cp "$BUILD/output/encoded.ivf" "$PWD/output/encoded.ivf" 2>/dev/null || true
cp "$BUILD/output/recon.yuv" "$PWD/output/recon.yuv" 2>/dev/null || true
cp -r "$BUILD/output/still_frames" "$PWD/output/" 2>/dev/null || true

if [ -f "$ENCODED_IVF" ] && [ -s "$ENCODED_IVF" ]; then
    "$FFMPEG_BIN" -y -i "$ENCODED_IVF" -pix_fmt yuv420p "$BUILD/output/decoded.yuv" 2>/dev/null || true
elif compgen -G "$BUILD/output/still_frames/frame_*.ivf" > /dev/null; then
    bash "$PWD/scripts/decode_still_sequence.sh" \
        "$BUILD/output/still_frames" \
        "$BUILD/output/decoded.yuv" \
        "$BUILD/output/output.mp4" \
        "$WIDTH" \
        "$HEIGHT" \
        "$FPS"
else
    "$FFMPEG_BIN" -y -f av1 -i "$BUILD/output/encoded.obu" -pix_fmt yuv420p "$BUILD/output/decoded.yuv" 2>/dev/null || true
fi

cp "$BUILD/output/decoded.yuv" "$PWD/output/decoded.yuv" 2>/dev/null || true
cp "$BUILD/output/output.mp4" "$PWD/output/output.mp4" 2>/dev/null || true

echo "=== Done ==="
