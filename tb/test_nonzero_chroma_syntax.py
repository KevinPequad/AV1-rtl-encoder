#!/usr/bin/env python3
"""Dynamic non-zero chroma syntax proof."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import test_rtl_obu_ivf_integrity as rtl_integrity

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 8

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

def cmp_file(a: Path, b: Path, label: str):
    if a.read_bytes() != b.read_bytes():
        raise SystemExit(f"[FAIL] {label}: {a} != {b}")
    print(f"[PASS] {label}")

require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=8 HEIGHT=8 all first")

with tempfile.TemporaryDirectory(prefix="av1_nonzero_chroma_") as td:
    t = Path(td)
    yuv = t / "delta8.yuv"
    out_obu = t / "encoded.obu"
    yuv.write_bytes(bytes([128] * (W * H)) + bytes([112] * ((W // 2) * (H // 2))) + bytes([144] * ((W // 2) * (H // 2))))
    run([str(SIM), "+frames=1", "+qindex=128", "+dc_only=1", "+all_key=1", "+ownership_strict=1", f"+input={yuv}", f"+output={out_obu}"])
    rtl_obu = t / "rtl_frames" / "frame_0000_rtl_raw.obu"
    rtl_ivf = t / "encoded_rtl.ivf"
    sw_ivf = t / "still_frames" / "frame_0000.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_rtl = t / "aom_rtl.yuv"
    cmp_file(out_obu, rtl_obu, "RTL raw OBU matches software oracle OBU")
    rtl_integrity.check_output_dir(t, expected_frames=1)
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf), "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf), "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")
print("[PASS] non-zero Cb/Cr TX_4X4 syntax proof")
