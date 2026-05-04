#!/usr/bin/env python3
"""32x32 two-frame natural-ish zero-MV inter public-decoder proof."""
from pathlib import Path
import os
import re

from av1_public_decode import artifact_dir, fail, public_decode_proof, run

TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
W = H = 32


def clip8(v: int) -> int:
    return max(0, min(255, v))


def write_probe(path: Path):
    # Frame 0 is the deterministic natural-ish gradient from the all-key 32x32 gate.
    # Frame 1 repeats the same source. Since frame 0 is quantized before becoming
    # the reference, frame 1 exercises non-zero inter residuals while keeping the
    # motion/repro path deterministic.
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            y[yy * W + xx] = clip8(96 + 3 * xx + 4 * yy + ((xx * yy) >> 4))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = clip8(112 + 2 * xx + 2 * yy + ((xx * yy) % 8))
            cr[yy * cw + xx] = clip8(148 - xx + yy + ((xx * 3 + yy * 5) % 8))
    one = bytes(y) + bytes(cb) + bytes(cr)
    path.write_bytes(one + one)


if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with artifact_dir("natural32_ip_syntax") as t:
    yuv = t / "natural32_2f.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([SIM, "+frames=2", "+qindex=128", "+dc_only=1", "+all_key=0", "+me_zero_mv_only=1", "+dump_inter_summary=1", f"+input={yuv}", f"+output={out_obu}"])
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
    public_decode_proof(
        output_dir=t,
        oracle_obu=out_obu,
        rtl_raw_obu=t / "encoded_rtl_raw.obu",
        sw_ivf=t / "encoded.ivf",
        rtl_ivf=t / "encoded_rtl.ivf",
        recon_yuv=t / "recon.yuv",
        label="32x32 two-frame natural-ish zero-MV inter RTL-owned proof",
    )

print("[PASS] 32x32 two-frame natural-ish zero-MV inter RTL-owned proof")
