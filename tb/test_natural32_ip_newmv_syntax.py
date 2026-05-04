#!/usr/bin/env python3
"""32x32 two-frame natural-ish inter proof with one isolated RTL-owned NEWMV block."""
from pathlib import Path
import os
import re

from av1_public_decode import artifact_dir, fail, public_decode_proof, run

TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
W = H = 32


def clip8(v: int) -> int:
    return max(0, min(255, v))


def base_planes():
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            y[yy * W + xx] = clip8(88 + 4 * xx + 3 * yy + ((xx * yy) >> 3) + ((xx ^ yy) & 7))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = clip8(106 + 3 * xx + yy + ((xx * yy) % 11))
            cr[yy * cw + xx] = clip8(156 - xx + 2 * yy + ((xx * 5 + yy * 3) % 9))
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


if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with artifact_dir("natural32_ip_newmv_syntax") as t:
    yuv = t / "natural32_2f_shifted.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([SIM, "+frames=2", "+qindex=128", "+dc_only=1", "+all_key=0", "+dump_inter_summary=1", "+me_newmv_limit=1", f"+input={yuv}", f"+output={out_obu}"])
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
    public_decode_proof(
        output_dir=t,
        oracle_obu=out_obu,
        rtl_raw_obu=t / "encoded_rtl_raw.obu",
        sw_ivf=t / "encoded.ivf",
        rtl_ivf=t / "encoded_rtl.ivf",
        recon_yuv=t / "recon.yuv",
        label="32x32 shifted two-frame isolated NEWMV inter RTL-owned proof",
    )

print("[PASS] 32x32 shifted two-frame isolated NEWMV inter RTL-owned proof")
