#!/usr/bin/env python3
"""64x64 two-frame inter proof with RTL-owned fractional q3 NEWMV coverage.

The documented BBB-derived data/natural_motion64_x640_y360_2f.yuv asset is not
present in this checkout. This guard regenerates a deterministic 64x64 surrogate:
a neutral frame with a small dark 8x8 luma object, followed by an AV1 regular-filter
half-pel shifted frame. The fixture is intentionally small enough to stay inside the
currently verified reduced keyframe/coeff subset while still requiring real
fractional-q3 motion vectors on the 64x64 LAST path.

Ownership rule: the testbench may compare against the software writer oracle, but
public decoder compatibility must come from the RTL raw OBU and IVF bytes without
repair, padding, or backpatching by this script.
"""
from pathlib import Path
import hashlib
import re
import shutil
import subprocess

TB = Path(__file__).resolve().parent
REPO = TB.parent
SIM = TB / "Vav1_encoder_top"
W = H = 64
BLOCK = 8
EXPECTED_INTER_BLOCKS = (W // BLOCK) * (H // BLOCK)
HALFPEL_COEFF = (0, 2, -14, 76, 76, -14, 2, 0)
DATA = REPO / "data" / "natural_motion64_x640_y360_2f.yuv"
OUTDIR = REPO / "output" / "natural_motion64_x640_y360_2f_subpel2"


def run(cmd, *, cwd=TB, check=True):
    print("[RUN]", " ".join(map(str, cmd)))
    res = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.stdout:
        print(res.stdout, end="")
    if check and res.returncode != 0:
        raise SystemExit(res.returncode)
    return res


def require(name):
    if shutil.which(name) is None:
        raise SystemExit(f"missing required tool: {name}")


def fail(msg: str):
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def cmp_file(a: Path, b: Path, label: str):
    if a.read_bytes() != b.read_bytes():
        fail(f"{label}: {a} != {b} (sizes {a.stat().st_size} vs {b.stat().st_size})")
    print(f"[PASS] {label} sha256={hashlib.sha256(a.read_bytes()).hexdigest()}")


def cmp_decoded(decoded: Path, recon: Path, label: str):
    dec = decoded.read_bytes()
    rec = recon.read_bytes()
    if dec != rec:
        frame_size = W * H * 3 // 2
        for idx, (d, r) in enumerate(zip(dec, rec)):
            if d != r:
                fail(f"{label}: first mismatch byte={idx} frame={idx // frame_size} plane_off={idx % frame_size} decoded={d} recon={r}")
        fail(f"{label}: size mismatch {len(dec)} vs {len(rec)}")
    print(f"[PASS] {label} sha256={hashlib.sha256(dec).hexdigest()}")


def clip8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def horizontal_halfpel(src: bytes, w: int, h: int) -> bytes:
    out = bytearray(w * h)
    for yy in range(h):
        row = yy * w
        for xx in range(w):
            acc = 0
            for tap, coeff in enumerate(HALFPEL_COEFF):
                sx = min(w - 1, max(0, xx + tap - 3))
                acc += coeff * src[row + sx]
            out[row + xx] = clip8((acc + 64) >> 7)
    return bytes(out)


def write_probe(path: Path):
    y0 = bytearray([128] * (W * H))
    # A small dark object on a neutral background. The half-pel filtered second
    # frame below forces the RTL ME/predictor to select non-zero fractional q3 MVs
    # while avoiding unsupported large 64x64 natural-keyframe coefficient patterns.
    for yy in range(8):
        for xx in range(8):
            y0[yy * W + xx] = 96
    y1 = horizontal_halfpel(bytes(y0), W, H)
    cb = bytes([128] * (W * H // 4))
    cr = bytes([128] * (W * H // 4))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(y0) + cb + cr + y1 + cb + cr)
    print(f"[INFO] regenerated deterministic 64x64 2-frame YUV: {path} bytes={path.stat().st_size} sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")

write_probe(DATA)
if OUTDIR.exists():
    shutil.rmtree(OUTDIR)
OUTDIR.mkdir(parents=True, exist_ok=True)
out_obu = OUTDIR / "encoded.obu"

sim = run([
    str(SIM), "+frames=2", "+timeout=8000000", "+qindex=128", "+dc_only=1", "+all_key=0",
    "+dump_inter_summary=1", "+me_newmv_limit=2",
    f"+input={DATA}", f"+output={out_obu}",
])
log = sim.stdout or ""
if "[TB] Frame 0 (KEY)" not in log:
    fail("frame 0 was not encoded as KEY")
if "[TB] Frame 1 (INTER)" not in log:
    fail("frame 1 was not encoded as INTER")
summary = re.search(r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+)", log)
if not summary:
    fail("missing frame 1 inter summary")
total_inter = int(summary.group(1))
nonzero_inter = int(summary.group(2))
if total_inter != EXPECTED_INTER_BLOCKS:
    fail(f"expected {EXPECTED_INTER_BLOCKS} inter blocks in frame 1, saw {total_inter}")
if nonzero_inter <= 0:
    fail("expected at least one non-zero inter residual block")
mv_lines = [(int(b), int(x), int(y)) for b, x, y in re.findall(r"inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\)", log)]
if len(mv_lines) != EXPECTED_INTER_BLOCKS:
    fail(f"expected {EXPECTED_INTER_BLOCKS} frame-1 MV lines, saw {len(mv_lines)}")
newmv_lines = [(b, x, y) for b, x, y in mv_lines if x != 0 or y != 0]
if len(newmv_lines) != 2:
    fail(f"expected exactly two NEWMV blocks under +me_newmv_limit=2, saw {newmv_lines}")
frac_lines = [(b, x, y) for b, x, y in newmv_lines if (x % 8) != 0 or (y % 8) != 0]
if not frac_lines:
    fail(f"expected at least one fractional q3 MV, saw only integer-q3 MVs {newmv_lines}")
print(f"[PASS] exercised 64x64 fractional q3 NEWMV set {newmv_lines}")

rtl_obu = OUTDIR / "encoded_rtl_raw.obu"
rtl_ivf = OUTDIR / "encoded_rtl.ivf"
sw_ivf = OUTDIR / "encoded.ivf"
recon = OUTDIR / "recon.yuv"
ff_sw = OUTDIR / "decoded_ffmpeg_sw.yuv"
ff_rtl = OUTDIR / "decoded_ffmpeg_rtl.yuv"
aom_rtl = OUTDIR / "decoded_aom_rtl.yuv"

cmp_file(out_obu, rtl_obu, "concatenated RTL raw OBU matches software oracle OBU")
cmp_file(sw_ivf, rtl_ivf, "RTL IVF matches software oracle IVF")
run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
     "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
cmp_decoded(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
     "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
cmp_decoded(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
cmp_decoded(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] 64x64 two-frame fractional NEWMV inter RTL-owned proof")
