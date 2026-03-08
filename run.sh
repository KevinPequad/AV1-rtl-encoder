#!/bin/bash
set -e

echo "=== AV1 Verilog Encoder Pipeline ==="

mkdir -p $PWD/data
mkdir -p $PWD/output

WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FPS="${FPS:-24}"
CLIP_SECONDS="${CLIP_SECONDS:-10}"
QINDEX="${QINDEX:-128}"
FRAME_BYTES=$((WIDTH * HEIGHT * 3 / 2))
RAW_YUV="${RAW_YUV:-$PWD/data/raw_frames.yuv}"
ENCODED_OBU="${ENCODED_OBU:-$PWD/output/encoded.obu}"
OUTPUT_MP4="${OUTPUT_MP4:-$PWD/output/output.mp4}"

OUTPUT_DIR="$PWD/data"
export WIDTH HEIGHT FPS CLIP_SECONDS OUTPUT_DIR

echo "Step 1: Download and decode Big Buck Bunny (first ${CLIP_SECONDS} seconds at ${WIDTH}x${HEIGHT})..."
bash "$PWD/scripts/download_and_decode.sh"

echo ""
echo "Step 2: Convert raw YUV to memory format..."
python3 "$PWD/scripts/yuv_to_mem.py" "$RAW_YUV" "$PWD/data"

if [ ! -s "$RAW_YUV" ]; then
    echo "ERROR: Missing raw YUV input: $RAW_YUV" >&2
    exit 1
fi

RAW_SIZE=$(stat -c%s "$RAW_YUV")
FRAME_COUNT=$((RAW_SIZE / FRAME_BYTES))
TIMEOUT="${TIMEOUT:-500000000}"

if [ "$FRAME_COUNT" -le 0 ]; then
    echo "ERROR: No complete YUV frames found in $RAW_YUV" >&2
    exit 1
fi

echo "Frame geometry: ${WIDTH}x${HEIGHT} @ ${FPS} fps"
echo "Raw input size: ${RAW_SIZE} bytes"
echo "Frame count:    ${FRAME_COUNT}"
echo "QIndex:         ${QINDEX}"
echo "Sim timeout:    ${TIMEOUT} cycles"

echo ""
echo "Step 3: Build Verilator simulation..."
ORIG_DIR="$PWD"
rm -rf /tmp/av1_build
mkdir -p /tmp/av1_build/tb
cp -r "$PWD/rtl" /tmp/av1_build/
cp -r "$PWD/tb" /tmp/av1_build/
cd /tmp/av1_build/tb
make clean && make WIDTH=$WIDTH HEIGHT=$HEIGHT
cp Vav1_encoder_top "$ORIG_DIR/tb/"
cd "$ORIG_DIR/tb"

echo ""
echo "Step 4: Run AV1 encoder simulation..."
rm -f "$ENCODED_OBU" "$OUTPUT_MP4"
./Vav1_encoder_top \
    +frames="$FRAME_COUNT" \
    +timeout="$TIMEOUT" \
    +qindex="$QINDEX" \
    +input="$RAW_YUV" \
    +output="$ENCODED_OBU"

echo ""
echo "Step 5: Package encoded bitstream to MP4..."
python3 "$ORIG_DIR/scripts/package_mp4.py" \
    "$ENCODED_OBU" \
    "$OUTPUT_MP4" \
    --fps "$FPS" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --raw-yuv "$RAW_YUV"

echo ""
echo "Step 6: Verify output against ffmpeg reference..."
# Decode the RTL output and compare with original
ffmpeg -y -f av1 -i "$ENCODED_OBU" -pix_fmt yuv420p "$PWD/output/decoded.yuv" 2>/dev/null || true

if [ -f "$PWD/output/decoded.yuv" ] && [ -s "$PWD/output/decoded.yuv" ]; then
    echo "Decoded RTL output. Running PSNR/SSIM comparison..."
    python3 "$ORIG_DIR/scripts/calc_psnr.py" "$RAW_YUV" "$PWD/output/decoded.yuv" "$WIDTH" "$HEIGHT" "$FPS"

    # Extract frames for visual comparison
    ffmpeg -y -f rawvideo -pix_fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$FPS" \
        -i "$RAW_YUV" -frames:v 4 "$PWD/output/orig_%02d.png" 2>/dev/null || true
    ffmpeg -y -f rawvideo -pix_fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$FPS" \
        -i "$PWD/output/decoded.yuv" -frames:v 4 "$PWD/output/decoded_%02d.png" 2>/dev/null || true

    echo "Visual comparison frames saved to output/"
else
    echo "WARNING: Could not decode RTL output with ffmpeg"
    echo "The bitstream may need further refinement"
fi

# Also compare with ffmpeg's own AV1 encode
if [ -f "$PWD/data/ffmpeg_reference.ivf" ]; then
    echo ""
    echo "ffmpeg reference AV1 encode exists for comparison."
    echo "RTL output: $(stat -c%s "$ENCODED_OBU" 2>/dev/null || echo '0') bytes"
    echo "ffmpeg ref:  $(stat -c%s "$PWD/data/ffmpeg_reference.ivf" 2>/dev/null || echo '0') bytes"
fi

echo ""
echo "=== Done! Output: $OUTPUT_MP4 ==="
