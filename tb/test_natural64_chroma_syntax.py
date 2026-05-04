#!/usr/bin/env python3
"""64x64 sparse flat-luma / non-zero-chroma public-decoder proof."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 64


def run(cmd, *, cwd=TB, check=True):
    print("[RUN]", " ".join(map(str, cmd)), flush=True)
    proc = subprocess.Popen(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines = []
    assert proc.stdout is not None
    for line in proc.stdout:
        lines.append(line)
        print(line, end="", flush=True)
    rc = proc.wait()
    stdout = "".join(lines)
    if check and rc != 0:
        raise SystemExit(rc)
    return subprocess.CompletedProcess(cmd, rc, stdout=stdout)


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


def clip8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def write_probe(path: Path):
    y = bytearray([128] * (W * H))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    cb_bias = [
        [0, 12, 0, -8],
        [10, 0, -6, 0],
        [0, -4, 14, 0],
        [-8, 0, 0, 6],
    ]
    cr_bias = [
        [0, -10, 0, 8],
        [-12, 0, 7, 0],
        [0, 5, -14, 0],
        [9, 0, 0, -6],
    ]
    for yy in range(ch):
        for xx in range(cw):
            bx = xx // 8
            by = yy // 8
            local = ((xx % 8) // 4) + ((yy % 8) // 4)
            cb[yy * cw + xx] = clip8(128 + cb_bias[by][bx] + local)
            cr[yy * cw + xx] = clip8(128 + cr_bias[by][bx] - local)
    path.write_bytes(bytes(y) + bytes(cb) + bytes(cr))


def parse_chroma_summary(log: str, frame: int):
    pat = re.compile(
        rf"chroma_summary frame={frame} "
        r"cb_nonzero_blocks=(\d+) cr_nonzero_blocks=(\d+) "
        r"cb_nonzero_coeffs=(\d+) cr_nonzero_coeffs=(\d+) "
        r"cb_inter_nonzero_blocks=(\d+) cr_inter_nonzero_blocks=(\d+) "
        r"chroma_only_blocks=(\d+) "
        r"inter_prev_cb_reads=(\d+) inter_prev_cr_reads=(\d+) "
        r"neigh_cb_reads=(\d+) neigh_cr_reads=(\d+)"
    )
    m = pat.search(log)
    if not m:
        fail(f"missing frame {frame} chroma summary")
    keys = [
        "cb_nonzero_blocks", "cr_nonzero_blocks",
        "cb_nonzero_coeffs", "cr_nonzero_coeffs",
        "cb_inter_nonzero_blocks", "cr_inter_nonzero_blocks",
        "chroma_only_blocks", "inter_prev_cb_reads", "inter_prev_cr_reads",
        "neigh_cb_reads", "neigh_cr_reads",
    ]
    return {k: int(v) for k, v in zip(keys, m.groups())}


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")

with tempfile.TemporaryDirectory(prefix="av1_natural64_chroma_") as td:
    t = Path(td)
    yuv = t / "natural64.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([
        str(SIM), "+frames=1", "+timeout=200000", "+progress_every=10000",
        "+qindex=128", "+dc_only=1", "+all_key=1", "+dump_chroma_summary=1",
        f"+input={yuv}", f"+output={out_obu}",
    ])
    log = sim.stdout or ""
    if "[TB] Frame 0 (KEY)" not in log:
        fail("frame 0 was not encoded as KEY")
    stats = parse_chroma_summary(log, 0)
    if stats["cb_nonzero_blocks"] < 4 or stats["cr_nonzero_blocks"] < 4:
        fail(f"expected sparse non-zero Cb/Cr coverage across multiple regions, saw {stats}")
    if stats["cb_nonzero_coeffs"] <= 0 or stats["cr_nonzero_coeffs"] <= 0:
        fail(f"expected non-zero Cb and Cr coefficients, saw {stats}")
    print(f"[PASS] sparse 64x64 chroma exercise counts: {stats}")

    rtl_obu = t / "rtl_frames" / "frame_0000_rtl_raw.obu"
    sw_ivf = t / "still_frames" / "frame_0000.ivf"
    rtl_ivf = t / "encoded_rtl.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_sw = t / "aom_sw.yuv"
    aom_rtl = t / "aom_rtl.yuv"

    cmp_file(out_obu, rtl_obu, "RTL raw OBU matches software oracle OBU")
    if sw_ivf.read_bytes() == rtl_ivf.read_bytes():
        print("[PASS] RTL IVF matches software still-picture IVF")
    else:
        print(
            "[INFO] software still-picture IVF and RTL sequence IVF differ as containers "
            f"(sizes {sw_ivf.stat().st_size} vs {rtl_ivf.stat().st_size}); raw OBU equality remains mandatory"
        )
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_sw), str(sw_ivf)])
    cmp_file(aom_sw, recon, "aomdec software IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] 64x64 sparse flat-luma / non-zero-chroma reduced-TX_4X4/dc_only P6 proof")
