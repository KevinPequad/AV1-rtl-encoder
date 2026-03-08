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
DC_ONLY="${DC_ONLY:-1}"
THREADS="${THREADS:-$(nproc)}"
BUILD_JOBS="${BUILD_JOBS:-$THREADS}"
FRAME_BYTES=$((WIDTH * HEIGHT * 3 / 2))
RAW_YUV="${RAW_YUV:-$PWD/data/raw_frames.yuv}"
ENCODED_OBU="${ENCODED_OBU:-$PWD/output/encoded.obu}"
OUTPUT_MP4="${OUTPUT_MP4:-$PWD/output/output.mp4}"
DECODED_YUV="$PWD/output/decoded.yuv"
STILL_DIR="$PWD/output/still_frames"
CPU_LIST="${CPU_LIST:-0-$((THREADS - 1))}"

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
if [ -z "${TIMEOUT:-}" ]; then
    TIMEOUT=$((FRAME_COUNT * 25000000 + 50000000))
fi

if [ "$FRAME_COUNT" -le 0 ]; then
    echo "ERROR: No complete YUV frames found in $RAW_YUV" >&2
    exit 1
fi

echo "Frame geometry: ${WIDTH}x${HEIGHT} @ ${FPS} fps"
echo "Raw input size: ${RAW_SIZE} bytes"
echo "Frame count:    ${FRAME_COUNT}"
echo "QIndex:         ${QINDEX}"
echo "Coeff mode:     $([ "$DC_ONLY" = "1" ] && echo "DC-only" || echo "full")"
echo "Sim threads:    ${THREADS}"
echo "Sim timeout:    ${TIMEOUT} cycles"

echo ""
echo "Step 3: Build Verilator simulation..."
ORIG_DIR="$PWD"
rm -rf /tmp/av1_build
mkdir -p /tmp/av1_build/tb
cp -r "$PWD/rtl" /tmp/av1_build/
cp -r "$PWD/tb" /tmp/av1_build/
cd /tmp/av1_build/tb
make clean && make WIDTH=$WIDTH HEIGHT=$HEIGHT THREADS=$THREADS BUILD_JOBS=$BUILD_JOBS
cp Vav1_encoder_top "$ORIG_DIR/tb/"
cd "$ORIG_DIR/tb"

echo ""
echo "Step 4: Run AV1 encoder simulation..."
rm -f "$ENCODED_OBU" "$OUTPUT_MP4"
SIM_CMD=(./Vav1_encoder_top \
    +frames="$FRAME_COUNT" \
    +timeout="$TIMEOUT" \
    +qindex="$QINDEX" \
    +dc_only="$DC_ONLY" \
    +input="$RAW_YUV" \
    +output="$ENCODED_OBU")
if command -v taskset >/dev/null 2>&1; then
    echo "CPU affinity:   ${CPU_LIST}"
    taskset -c "$CPU_LIST" "${SIM_CMD[@]}"
else
    "${SIM_CMD[@]}"
fi

echo ""
if compgen -G "$STILL_DIR/frame_*.ivf" > /dev/null; then
    echo "Step 5: Decode AV1 still-frame sequence and package MP4..."
    bash "$ORIG_DIR/scripts/decode_still_sequence.sh" \
        "$STILL_DIR" \
        "$DECODED_YUV" \
        "$OUTPUT_MP4" \
        "$WIDTH" \
        "$HEIGHT" \
        "$FPS"
else
    echo "Step 5: Package encoded bitstream to MP4..."
    python3 "$ORIG_DIR/scripts/package_mp4.py" \
        "$ENCODED_OBU" \
        "$OUTPUT_MP4" \
        --fps "$FPS" \
        --width "$WIDTH" \
        --height "$HEIGHT" \
        --raw-yuv "$RAW_YUV"
fi

echo ""
echo "Step 6: Verify output against ffmpeg reference..."
if [ -f "$DECODED_YUV" ] && [ -s "$DECODED_YUV" ]; then
    echo "Decoded RTL output. Running PSNR/SSIM comparison..."
    python3 "$ORIG_DIR/scripts/calc_psnr.py" "$RAW_YUV" "$DECODED_YUV" "$WIDTH" "$HEIGHT" "$FPS"

    if [ -f "$PWD/data/ffmpeg_reference.ivf" ]; then
        FFMPEG_REF_YUV="$PWD/output/ffmpeg_reference_decoded.yuv"
        ffmpeg -y -i "$PWD/data/ffmpeg_reference.ivf" -pix_fmt yuv420p "$FFMPEG_REF_YUV" 2>/dev/null || true
        if [ -f "$FFMPEG_REF_YUV" ] && [ -s "$FFMPEG_REF_YUV" ]; then
            echo ""
            echo "ffmpeg AV1 reference PSNR/SSIM baseline..."
            python3 "$ORIG_DIR/scripts/calc_psnr.py" "$RAW_YUV" "$FFMPEG_REF_YUV" "$WIDTH" "$HEIGHT" "$FPS"
        fi
    fi

    # Extract frames for visual comparison
    ffmpeg -y -f rawvideo -pix_fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$FPS" \
        -i "$RAW_YUV" -frames:v 4 "$PWD/output/orig_%02d.png" 2>/dev/null || true
    ffmpeg -y -f rawvideo -pix_fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$FPS" \
        -i "$DECODED_YUV" -frames:v 4 "$PWD/output/decoded_%02d.png" 2>/dev/null || true

    echo "Visual comparison frames saved to output/"
else
    echo "WARNING: Could not build a decoded RTL output sequence"
fi

if [ -f "$PWD/data/ffmpeg_reference.ivf" ]; then
    echo ""
    echo "ffmpeg reference AV1 encode exists for comparison."
    echo "RTL raw bitstream debug dump: $(stat -c%s "$ENCODED_OBU" 2>/dev/null || echo '0') bytes"
    echo "ffmpeg ref:  $(stat -c%s "$PWD/data/ffmpeg_reference.ivf" 2>/dev/null || echo '0') bytes"
fi

echo ""
echo "=== Done! Output: $OUTPUT_MP4 ==="
