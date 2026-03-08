#!/bin/bash
set -e
BUILD=/tmp/av1_build
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp -r "$PWD/rtl" "$BUILD/"
cp -r "$PWD/tb" "$BUILD/"
cp -r "$PWD/data" "$BUILD/" 2>/dev/null || mkdir -p "$BUILD/data"
mkdir -p "$BUILD/output"

cd "$BUILD/tb"
make clean && make
echo "=== Build OK ==="

FRAMES=${1:-1}
QINDEX=${2:-128}
TIMEOUT=${3:-500000000}
./Vav1_encoder_top +frames=$FRAMES +qindex=$QINDEX \
    +input=$BUILD/data/raw_frames.yuv \
    +output=$BUILD/output/encoded.obu +timeout=$TIMEOUT 2>&1

cp "$BUILD/output/encoded.obu" "$PWD/output/encoded.obu" 2>/dev/null || true
cp "$BUILD/output/recon.yuv" "$PWD/output/recon.yuv" 2>/dev/null || true

# Decode and compare
ffmpeg -y -f av1 -i "$BUILD/output/encoded.obu" -pix_fmt yuv420p "$BUILD/output/decoded.yuv" 2>/dev/null || true
cp "$BUILD/output/decoded.yuv" "$PWD/output/decoded.yuv" 2>/dev/null || true

echo "=== Done ==="
