#!/usr/bin/env python3
"""P4 current 64x64->8x8 partition-tree proof.

This is a syntax-visible gate: it requires the RTL-emitted raw OBU to match the
software oracle OBU and both public decoders to match the RTL reconstruction.
The partition dump is captured from accepted RTL entropy symbols, not repaired
or inferred from the writer.
"""
from pathlib import Path
import os
from collections import Counter
import re
import shutil
import subprocess
import tempfile

TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
W = H = 64
EXPECTED_COUNTS = {6: 1, 5: 4, 4: 16, 3: 64}


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
            # Deterministic, low-amplitude natural-ish texture.
            y[yy * W + xx] = max(0, min(255, 96 + 2 * xx + 3 * yy + ((xx * yy) >> 5)))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = max(0, min(255, 116 + xx + yy + ((xx * yy) & 3)))
            cr[yy * cw + xx] = max(0, min(255, 140 - (xx >> 1) + yy + ((xx + 3 * yy) & 3)))
    path.write_bytes(bytes(y) + bytes(cb) + bytes(cr))


def validate_partitions(log: str):
    pat = re.compile(r"\[P4_PART\] frame=(\d+) blk=\((\d+),(\d+)\) log2=(\d+) symbol=(\d+) nsyms=(\d+)")
    entries = []
    for m in pat.finditer(log):
        frame, bx, by, log2, symbol, nsyms = map(int, m.groups())
        if frame != 0:
            fail(f"unexpected partition dump for frame {frame}")
        entries.append((bx, by, log2, symbol, nsyms))
    if not entries:
        fail("no RTL partition dump entries found; did +dump_partition=1 reach accepted entropy symbols?")
    counts = Counter(e[2] for e in entries)
    if dict(counts) != EXPECTED_COUNTS:
        fail(f"partition log2 counts got {dict(counts)} expected {EXPECTED_COUNTS}")
    for bx, by, log2, symbol, nsyms in entries:
        if log2 == 3:
            if symbol != 0 or nsyms != 4:
                fail(f"8x8 leaf blk=({bx},{by}) encoded symbol={symbol} nsyms={nsyms}, expected PARTITION_NONE/4")
        else:
            if symbol != 3 or nsyms != 10:
                fail(f"log2={log2} blk=({bx},{by}) encoded symbol={symbol} nsyms={nsyms}, expected SPLIT/10")
    print(f"[PASS] RTL partition tree accepted counts {dict(counts)}")


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")

with tempfile.TemporaryDirectory(prefix="av1_p4_partition64_") as td:
    t = Path(td)
    yuv = t / "partition64.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([str(SIM), "+frames=1", "+qindex=128", "+dc_only=1", "+all_key=1", "+dump_partition=1",
               f"+input={yuv}", f"+output={out_obu}"])
    validate_partitions(sim.stdout or "")

    rtl_obu = t / "rtl_frames" / "frame_0000_rtl_raw.obu"
    sw_ivf = t / "still_frames" / "frame_0000.ivf"
    rtl_ivf = t / "encoded_rtl.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_rtl = t / "aom_rtl.yuv"

    cmp_file(out_obu, rtl_obu, "RTL raw OBU matches software oracle OBU")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] P4 current 64x64-to-8x8 partition-tree gate")
