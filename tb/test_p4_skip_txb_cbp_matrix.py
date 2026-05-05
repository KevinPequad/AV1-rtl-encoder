#!/usr/bin/env python3
"""P4 skip/txb_skip/CBP matrix for the current 8x8 luma + 4x4 chroma lane."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 8
CASES = {
    "all_zero":  {"y": 128, "cb": 128, "cr": 128, "expect": (0, 0, 0, 1)},
    "luma_only": {"y": 176, "cb": 128, "cr": 128, "expect": (1, 0, 0, 0)},
    "cb_only":   {"y": 128, "cb": 96,  "cr": 128, "expect": (0, 1, 0, 0)},
    "cr_only":   {"y": 128, "cb": 128, "cr": 160, "expect": (0, 0, 1, 0)},
    "mixed":     {"y": 176, "cb": 96,  "cr": 160, "expect": (1, 1, 1, 0)},
}


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


def write_case(path: Path, yv: int, cbv: int, crv: int):
    y = bytes([yv] * (W * H))
    cb = bytes([cbv] * ((W // 2) * (H // 2)))
    cr = bytes([crv] * ((W // 2) * (H // 2)))
    path.write_bytes(y + cb + cr)


def parse_cbp(log: str, name: str):
    pat = re.compile(r"\[P4_CBP\] frame=0 blk=0 luma=(\d+) cb=(\d+) cr=(\d+) skip=(\d+) luma_nz=(\d+) cb_nz=(\d+) cr_nz=(\d+)")
    m = pat.search(log)
    if not m:
        fail(f"{name}: missing P4_CBP dump for block 0")
    vals = tuple(map(int, m.groups()[:4]))
    nz = tuple(map(int, m.groups()[4:]))
    print(f"[PASS] {name} CBP dump luma/cb/cr/skip={vals} nz={nz}")
    return vals


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=8 HEIGHT=8 all first")

with tempfile.TemporaryDirectory(prefix="av1_p4_skip_cbp_") as td:
    root = Path(td)
    for name, cfg in CASES.items():
        t = root / name
        t.mkdir()
        yuv = t / f"{name}.yuv"
        out_obu = t / "encoded.obu"
        write_case(yuv, cfg["y"], cfg["cb"], cfg["cr"])
        sim = run([str(SIM), "+frames=1", "+qindex=128", "+dc_only=1", "+all_key=1", "+dump_blocks=1",
                   f"+input={yuv}", f"+output={out_obu}"])
        got = parse_cbp(sim.stdout or "", name)
        if got != cfg["expect"]:
            fail(f"{name}: expected luma/cb/cr/skip={cfg['expect']} got {got}")

        rtl_obu = t / "rtl_frames" / "frame_0000_rtl_raw.obu"
        sw_ivf = t / "still_frames" / "frame_0000.ivf"
        rtl_ivf = t / "encoded_rtl.ivf"
        recon = t / "recon.yuv"
        ff_sw = t / "ff_sw.yuv"
        ff_rtl = t / "ff_rtl.yuv"
        aom_rtl = t / "aom_rtl.yuv"

        cmp_file(out_obu, rtl_obu, f"{name} RTL raw OBU matches software oracle OBU")
        run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
             "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
        cmp_file(ff_sw, recon, f"{name} FFmpeg software IVF decode matches RTL recon")
        run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
             "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
        cmp_file(ff_rtl, recon, f"{name} FFmpeg RTL IVF decode matches RTL recon")
        run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
        cmp_file(aom_rtl, recon, f"{name} aomdec RTL IVF decode matches RTL recon")

print("[PASS] P4 skip/txb_skip/CBP all-zero/luma/Cb/Cr/mixed matrix")
