#!/usr/bin/env python3
"""32x32 two-frame natural-ish zero-MV inter public-decoder proof."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import test_rtl_obu_ivf_integrity as rtl_integrity

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 32


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
    print(f"[PASS] {label}")


def write_probe(path: Path):
    # Frame 0 is the deterministic natural-ish gradient from the all-key 32x32 gate.
    # Frame 1 repeats the same source. Since frame 0 is quantized before becoming
    # the reference, frame 1 exercises non-zero inter residuals while keeping the
    # motion/repro path deterministic.
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            y[yy * W + xx] = max(0, min(255, 96 + 3 * xx + 4 * yy + ((xx * yy) >> 4)))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = max(0, min(255, 112 + 2 * xx + 2 * yy + ((xx * yy) % 8)))
            cr[yy * cw + xx] = max(0, min(255, 148 - xx + yy + ((xx * 3 + yy * 5) % 8)))
    one = bytes(y) + bytes(cb) + bytes(cr)
    path.write_bytes(one + one)


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with tempfile.TemporaryDirectory(prefix="av1_natural32_ip_") as td:
    t = Path(td)
    yuv = t / "natural32_2f.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([str(SIM), "+frames=2", "+qindex=128", "+dc_only=1", "+all_key=0", "+me_zero_mv_only=1", "+dump_inter_summary=1", "+ownership_strict=1",
              f"+input={yuv}", f"+output={out_obu}"])
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
    if total_inter != 16:
        fail(f"expected 16 inter blocks in frame 1, saw {total_inter}")
    if nonzero_inter <= 0:
        fail("expected at least one non-zero inter residual block")
    mv_lines = re.findall(r"inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\)", log)
    if len(mv_lines) != 16:
        fail(f"expected 16 frame-1 MV lines, saw {len(mv_lines)}")
    bad_mvs = [(int(b), int(x), int(y)) for b, x, y in mv_lines if int(x) != 0 or int(y) != 0]
    if bad_mvs:
        fail(f"expected all frame-1 MVs to be zero, saw {bad_mvs[:4]}")

    rtl_obu = t / "encoded_rtl_raw.obu"
    rtl_ivf = t / "encoded_rtl.ivf"
    sw_ivf = t / "encoded.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_rtl = t / "aom_rtl.yuv"

    cmp_file(out_obu, rtl_obu, "concatenated RTL raw OBU matches software oracle OBU")
    rtl_integrity.check_output_dir(t, expected_frames=2)
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] 32x32 two-frame natural-ish zero-MV inter RTL-owned proof")
