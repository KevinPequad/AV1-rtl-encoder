#!/bin/bash
set -e
BUILD=/tmp/av1_build
WIDTH=${WIDTH:-1280}
HEIGHT=${HEIGHT:-720}
FPS=${FPS:-24}
THREADS=${THREADS:-$(nproc)}
BUILD_JOBS=${BUILD_JOBS:-$THREADS}
CPU_LIST=${CPU_LIST:-0-$((THREADS - 1))}
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
SIM_CMD=(./Vav1_encoder_top +frames=$FRAMES +qindex=$QINDEX \
    +input=$BUILD/data/raw_frames.yuv \
    +output=$BUILD/output/encoded.obu +timeout=$TIMEOUT +dc_only=$DC_ONLY)
if command -v taskset >/dev/null 2>&1; then
    taskset -c "$CPU_LIST" "${SIM_CMD[@]}" 2>&1
else
    "${SIM_CMD[@]}" 2>&1
fi

cp "$BUILD/output/encoded.obu" "$PWD/output/encoded.obu" 2>/dev/null || true
cp "$BUILD/output/recon.yuv" "$PWD/output/recon.yuv" 2>/dev/null || true
cp -r "$BUILD/output/still_frames" "$PWD/output/" 2>/dev/null || true

if compgen -G "$BUILD/output/still_frames/frame_*.ivf" > /dev/null; then
    bash "$PWD/scripts/decode_still_sequence.sh" \
        "$BUILD/output/still_frames" \
        "$BUILD/output/decoded.yuv" \
        "$BUILD/output/output.mp4" \
        "$WIDTH" \
        "$HEIGHT" \
        "$FPS"
else
    ffmpeg -y -f av1 -i "$BUILD/output/encoded.obu" -pix_fmt yuv420p "$BUILD/output/decoded.yuv" 2>/dev/null || true
fi

cp "$BUILD/output/decoded.yuv" "$PWD/output/decoded.yuv" 2>/dev/null || true
cp "$BUILD/output/output.mp4" "$PWD/output/output.mp4" 2>/dev/null || true

echo "=== Done ==="
