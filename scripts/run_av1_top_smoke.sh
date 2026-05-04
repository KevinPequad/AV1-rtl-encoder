#!/usr/bin/env bash
# Worktree-safe Chud PC 2 AV1 top-level smoke.
# Runs from the current repo checkout/worktree; it does not read or modify the
# canonical checkout unless this script itself is invoked from canonical.
set -euo pipefail

REPO=${AV1_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
if [[ -n "${AV1_ARTIFACT_ROOT:-}" ]]; then
  WORK="$AV1_ARTIFACT_ROOT/top_smoke"
else
  WORK=${AV1_TOP_SMOKE_WORK:-/tmp/av1_chudpc2_top_smoke_worktree}
fi
THREADS=${THREADS:-16}
BUILD_JOBS=${BUILD_JOBS:-16}
export WORK

rm -rf "$WORK"
mkdir -p "$WORK"/data "$WORK"/output
cp -r "$REPO/rtl" "$WORK/"
cp -r "$REPO/tb" "$WORK/"

python3 - <<'PY'
from pathlib import Path
W = 16
H = 16
out = Path('${WORK:-/tmp}/data')
# The shell does not expand inside single-quoted heredocs, so recompute from env.
import os
out = Path(os.environ.get('AV1_TOP_SMOKE_DATA', '')) if os.environ.get('AV1_TOP_SMOKE_DATA') else Path(os.environ['WORK']) / 'data'
out.mkdir(parents=True, exist_ok=True)
y = bytearray()
for yy in range(H):
    for xx in range(W):
        y.append((96 + xx * 2 + yy * 3) & 255)
uv = bytes([128]) * (W * H // 2)
frame = bytes(y) + uv
(out / 'grad16_1f.yuv').write_bytes(frame)
(out / 'grad16_2f_repeat.yuv').write_bytes(frame + frame)
PY

cd "$WORK/tb"
make clean >/dev/null 2>/dev/null || true
/usr/bin/time -f 'ELAPSED top-build %e' \
  make WIDTH=16 HEIGHT=16 THREADS="$THREADS" BUILD_JOBS="$BUILD_JOBS" \
  >"$WORK/top_build.log" 2>"$WORK/top_build.err" || {
    tail -120 "$WORK/top_build.log"
    tail -120 "$WORK/top_build.err"
    exit 1
  }
tail -20 "$WORK/top_build.log"
tail -20 "$WORK/top_build.err"

run_case() {
  name="$1"; frames="$2"; input="$3"; all_key="$4"; timeout="$5"
  outdir="$WORK/output/$name"; mkdir -p "$outdir"
  echo "=== RUN $name frames=$frames all_key=$all_key ==="
  /usr/bin/time -f "ELAPSED $name %e" \
    ./Vav1_encoder_top \
      +frames="$frames" +timeout="$timeout" +qindex=128 +dc_only=1 +all_key="$all_key" \
      +input="$input" +output="$outdir/encoded.obu" \
      >"$outdir/sim.out" 2>"$outdir/sim.err" || {
        tail -120 "$outdir/sim.out"
        tail -120 "$outdir/sim.err"
        exit 1
      }
  tail -80 "$outdir/sim.out"
  tail -80 "$outdir/sim.err"
  cmp -s "$outdir/encoded.obu" "$outdir/encoded_rtl_raw.obu" && echo "CMP obu software-vs-rtl: PASS" || {
    echo "CMP obu software-vs-rtl: FAIL"
    cmp -l "$outdir/encoded.obu" "$outdir/encoded_rtl_raw.obu" | head
    exit 1
  }
  cmp -s "$outdir/encoded.ivf" "$outdir/encoded_rtl.ivf" && echo "CMP ivf software-vs-rtl: PASS" || {
    echo "CMP ivf software-vs-rtl: FAIL"
    exit 1
  }
  ffmpeg -y -v error -i "$outdir/encoded_rtl.ivf" -pix_fmt yuv420p "$outdir/decoded_ffmpeg.yuv"
  aomdec --codec=av1 --yv12 --rawvideo -o "$outdir/decoded_aom.yuv" "$outdir/encoded_rtl.ivf" \
    >"$outdir/aomdec.log" 2>"$outdir/aomdec.err" || {
      cat "$outdir/aomdec.log"
      cat "$outdir/aomdec.err"
      exit 1
    }
  cmp -s "$outdir/decoded_ffmpeg.yuv" "$outdir/recon.yuv" && echo "FFMPEG decode vs recon: PASS" || {
    echo "FFMPEG decode vs recon: FAIL"
    exit 1
  }
  cmp -s "$outdir/decoded_aom.yuv" "$outdir/recon.yuv" && echo "AOMDEC decode vs recon: PASS" || {
    echo "AOMDEC decode vs recon: FAIL"
    exit 1
  }
  sha256sum "$outdir/encoded_rtl_raw.obu" "$outdir/encoded_rtl.ivf" "$outdir/recon.yuv" "$outdir/decoded_ffmpeg.yuv" "$outdir/decoded_aom.yuv"
}

export WORK
run_case smoke16_1f_allkey 1 "$WORK/data/grad16_1f.yuv" 1 5000000
run_case smoke16_2f_ip_repeat 2 "$WORK/data/grad16_2f_repeat.yuv" 0 20000000
