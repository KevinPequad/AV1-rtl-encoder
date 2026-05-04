#!/usr/bin/env python3
"""32x32 two-frame natural-ish inter proof with one isolated RTL-owned NEWMV block."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

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


def base_planes():
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            y[yy * W + xx] = max(0, min(255, 88 + 4 * xx + 3 * yy + ((xx * yy) >> 3) + ((xx ^ yy) & 7)))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = max(0, min(255, 106 + 3 * xx + yy + ((xx * yy) % 11)))
            cr[yy * cw + xx] = max(0, min(255, 156 - xx + 2 * yy + ((xx * 5 + yy * 3) % 9)))
    return bytes(y), bytes(cb), bytes(cr)


def shifted(src: bytes, w: int, h: int, dx: int, dy: int) -> bytes:
    out = bytearray(w * h)
    for yy in range(h):
        for xx in range(w):
            sx = min(w - 1, max(0, xx - dx))
            sy = min(h - 1, max(0, yy - dy))
            out[yy * w + xx] = src[sy * w + sx]
    return bytes(out)


def write_probe(path: Path):
    y0, cb0, cr0 = base_planes()
    # Frame 1 is a one-pixel right shift of frame 0, so at least interior blocks
    # should prefer a non-zero LAST-frame MV rather than GLOBALMV/zero MV.
    y1 = shifted(y0, W, H, 1, 0)
    cb1 = shifted(cb0, W // 2, H // 2, 1, 0)
    cr1 = shifted(cr0, W // 2, H // 2, 1, 0)
    path.write_bytes(y0 + cb0 + cr0 + y1 + cb1 + cr1)


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with tempfile.TemporaryDirectory(prefix="av1_natural32_ip_newmv_") as td:
    t = Path(td)
    yuv = t / "natural32_2f_shifted.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([str(SIM), "+frames=2", "+qindex=128", "+dc_only=1", "+all_key=0", "+dump_inter_summary=1", "+me_newmv_limit=1",
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
    if total_inter <= 0:
        fail("expected at least one inter block in frame 1")
    if nonzero_inter <= 0:
        fail("expected at least one non-zero inter residual block")
    mv_lines = re.findall(r"inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\)", log)
    if not mv_lines:
        fail("missing frame-1 MV lines")
    newmv_lines = [(int(b), int(x), int(y)) for b, x, y in mv_lines if int(x) != 0 or int(y) != 0]
    if len(newmv_lines) != 1:
        fail(f"expected exactly one isolated non-zero MV/NEWMV block, saw {newmv_lines}")
    print(f"[PASS] exercised isolated non-zero-MV inter block {newmv_lines[0]}")

    rtl_obu = t / "encoded_rtl_raw.obu"
    rtl_ivf = t / "encoded_rtl.ivf"
    sw_ivf = t / "encoded.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_rtl = t / "aom_rtl.yuv"

    cmp_file(out_obu, rtl_obu, "concatenated RTL raw OBU matches software oracle OBU")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] 32x32 shifted two-frame isolated NEWMV inter RTL-owned proof")
