#!/usr/bin/env python3
"""P4 lossy qindex sweep for the current one-block 8x8/TX_8X8 lane."""
from pathlib import Path
import shutil
import subprocess
import tempfile

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 8
QINDICES = [1, 2, 4, 8, 16, 32, 64, 128, 192, 240, 255]


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


def fail(msg):
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def cmp_file(a: Path, b: Path, label: str):
    if a.read_bytes() != b.read_bytes():
        fail(f"{label}: {a} != {b} (sizes {a.stat().st_size} vs {b.stat().st_size})")
    print(f"[PASS] {label}")


def write_probe(path: Path):
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            # Keep low-q cases bounded enough for the current coefficient syntax
            # lane while still producing deterministic non-zero DC/AC.
            y[yy * W + xx] = 128 + ((xx + 2 * yy) & 3)
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = 128 + ((2 * xx + yy) & 3)
            cr[yy * cw + xx] = 128 - ((xx + 2 * yy) & 3)
    path.write_bytes(bytes(y) + bytes(cb) + bytes(cr))


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=8 HEIGHT=8 all first")

with tempfile.TemporaryDirectory(prefix="av1_p4_qindex_sweep_") as td:
    root = Path(td)
    yuv = root / "qindex_sweep.yuv"
    write_probe(yuv)
    for q in QINDICES:
        t = root / f"q{q:03d}"
        t.mkdir()
        out_obu = t / "encoded.obu"
        run([str(SIM), "+frames=1", f"+qindex={q}", "+dc_only=1", "+all_key=1", "+timeout=20000000",
             f"+input={yuv}", f"+output={out_obu}"])

        rtl_obu = t / "rtl_frames" / "frame_0000_rtl_raw.obu"
        sw_ivf = t / "still_frames" / "frame_0000.ivf"
        rtl_ivf = t / "encoded_rtl.ivf"
        recon = t / "recon.yuv"
        ff_sw = t / "ff_sw.yuv"
        ff_rtl = t / "ff_rtl.yuv"
        aom_rtl = t / "aom_rtl.yuv"

        cmp_file(out_obu, rtl_obu, f"qindex={q} RTL raw OBU matches software oracle OBU")
        run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
             "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
        cmp_file(ff_sw, recon, f"qindex={q} FFmpeg software IVF decode matches RTL recon")
        run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
             "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
        cmp_file(ff_rtl, recon, f"qindex={q} FFmpeg RTL IVF decode matches RTL recon")
        run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
        cmp_file(aom_rtl, recon, f"qindex={q} aomdec RTL IVF decode matches RTL recon")

print(f"[PASS] P4 lossy qindex sweep: {QINDICES}")
