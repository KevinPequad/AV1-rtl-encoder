#!/usr/bin/env python3
"""P5 high-DC qindex=1 public-decoder proof on the current lossy TX_8X8 lane."""
from __future__ import annotations

from pathlib import Path
import re
import tempfile

from av1_syntax_test_common import SIM, check_public_decoder_case, current_width_height, fail, require, run


def clip8(v: int) -> int:
    return max(0, min(255, v))


def write_highdc_probe(path: Path, width: int, height: int) -> None:
    if width % 8 or height % 8:
        fail(f"highdc probe requires 8x8 tiling, got {width}x{height}")
    y = bytearray(width * height)
    patterns = [
        lambda x, y: 40 + 8 * x + 5 * y,
        lambda x, y: 228 - 6 * x + 3 * y,
        lambda x, y: 56 + ((x + y) & 1) * 52 + x,
        lambda x, y: 200 - (((x * 13) + (y * 7)) & 31),
    ]
    blk_cols = width // 8
    blk_rows = height // 8
    blk = 0
    for by in range(blk_rows):
        for bx in range(blk_cols):
            pat = patterns[blk % len(patterns)]
            for yy in range(8):
                for xx in range(8):
                    px = bx * 8 + xx
                    py = by * 8 + yy
                    y[py * width + px] = clip8(pat(xx, yy))
            blk += 1
    chroma = bytes([128]) * ((width // 2) * (height // 2))
    path.write_bytes(bytes(y) + chroma + chroma)


COEFF_RE = re.compile(
    r"\[P5_COEFF\] frame=(?P<frame>\d+) blk=(?P<blk>\d+) eob=(?P<eob>\d+) "
    r"first_ac_scan=(?P<first_ac_scan>-?\d+) nz=(?P<nz>\d+) ac_nz=(?P<ac_nz>\d+) "
    r"dc=(?P<dc>-?\d+) abs_dc=(?P<abs_dc>\d+) max_abs=(?P<max_abs>\d+)"
)


def parse_coeff_stats(log: str) -> list[dict[str, int]]:
    stats = []
    for match in COEFF_RE.finditer(log):
        stats.append({k: int(v) for k, v in match.groupdict().items()})
    if not stats:
        fail("missing P5 coeff summary lines; did +dump_coeff_summary=1 reach the testbench?")
    return stats


require("ffmpeg", "aomdec")
if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=16 HEIGHT=16 all first")

W, H = current_width_height(16, 16)
label = f"P5 highdc q1 public {W}x{H}"
with tempfile.TemporaryDirectory(prefix=f"av1_p5_highdc_q1_{W}x{H}_") as td:
    root = Path(td)
    yuv = root / "highdc_q1.yuv"
    out_obu = root / "encoded.obu"
    write_highdc_probe(yuv, W, H)
    sim = run([
        str(SIM),
        "+frames=1",
        "+timeout=50000000",
        "+qindex=1",
        "+dc_only=0",
        "+all_key=1",
        "+dump_coeff_summary=1",
        f"+input={yuv}",
        f"+output={out_obu}",
    ])
    stats = parse_coeff_stats(sim.stdout or "")
    best = max(stats, key=lambda s: (s["abs_dc"], s["max_abs"], s["eob"], s["ac_nz"]))
    if best["abs_dc"] < 8:
        fail(f"{label}: expected a strong low-q DC stress block, best abs_dc={best['abs_dc']} stats={best}")
    if best["ac_nz"] < 1 or best["eob"] < 2:
        fail(f"{label}: expected non-zero AC stress alongside high DC, best stats={best}")
    check_public_decoder_case({
        "out_obu": out_obu,
        "sw_ivf": root / "encoded.ivf",
        "rtl_raw": root / "encoded_rtl_raw.obu",
        "rtl_ivf": root / "encoded_rtl.ivf",
        "recon": root / "recon.yuv",
    }, label)

print(f"[PASS] {label}: best block {best}")
