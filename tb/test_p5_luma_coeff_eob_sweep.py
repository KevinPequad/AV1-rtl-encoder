#!/usr/bin/env python3
"""P5 decoder-clean short/mid/long EOB probes on the current lossy TX_8X8 lane."""
from __future__ import annotations

from pathlib import Path
import re
import tempfile

from av1_syntax_test_common import SIM, check_public_decoder_case, current_width_height, fail, require, run


CASES = [
    {"name": "short_tail", "pattern": "hgrad", "amp": 1, "qindex": 64, "expect": lambda e: 2 <= e <= 4},
    {"name": "mid_tail", "pattern": "diag", "amp": 32, "qindex": 128, "expect": lambda e: 5 <= e <= 12},
    {"name": "long_tail", "pattern": "diag", "amp": 32, "qindex": 32, "expect": lambda e: e >= 13},
]


def clip8(v: int) -> int:
    return max(0, min(255, v))


def block_value(pattern: str, amp: int, x: int, y: int) -> int:
    if pattern == "hgrad":
        return 128 + amp * (x - 3)
    if pattern == "diag":
        return 128 + amp * (x + y - 7)
    fail(f"unknown pattern {pattern}")
    return 128


def write_probe(path: Path, width: int, height: int, pattern: str, amp: int) -> None:
    if width != 8 or height != 8:
        fail(f"current EOB probe is defined for one 8x8 luma block, got {width}x{height}")
    y = bytearray(width * height)
    for yy in range(height):
        for xx in range(width):
            y[yy * width + xx] = clip8(block_value(pattern, amp, xx, yy))
    chroma = bytes([128]) * ((width // 2) * (height // 2))
    path.write_bytes(bytes(y) + chroma + chroma)


COEFF_RE = re.compile(
    r"\[P5_COEFF\] frame=(?P<frame>\d+) blk=(?P<blk>\d+) eob=(?P<eob>\d+) "
    r"first_ac_scan=(?P<first_ac_scan>-?\d+) nz=(?P<nz>\d+) ac_nz=(?P<ac_nz>\d+) "
    r"dc=(?P<dc>-?\d+) abs_dc=(?P<abs_dc>\d+) max_abs=(?P<max_abs>\d+)"
)


def parse_single_coeff_stats(log: str) -> dict[str, int]:
    matches = [{k: int(v) for k, v in m.groupdict().items()} for m in COEFF_RE.finditer(log)]
    if len(matches) != 1:
        fail(f"expected exactly one coeff-summary block, saw {len(matches)}")
    return matches[0]


require("ffmpeg", "aomdec")
if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=8 HEIGHT=8 all first")

W, H = current_width_height(8, 8)
label = f"P5 luma coeff EOB sweep {W}x{H}"
seen = []
with tempfile.TemporaryDirectory(prefix=f"av1_p5_eob_sweep_{W}x{H}_") as td:
    root = Path(td)
    for case in CASES:
        t = root / case["name"]
        t.mkdir()
        yuv = t / f"{case['name']}.yuv"
        out_obu = t / "encoded.obu"
        write_probe(yuv, W, H, case["pattern"], case["amp"])
        sim = run([
            str(SIM),
            "+frames=1",
            "+timeout=50000000",
            f"+qindex={case['qindex']}",
            "+dc_only=0",
            "+all_key=1",
            "+dump_coeff_summary=1",
            f"+input={yuv}",
            f"+output={out_obu}",
        ])
        stats = parse_single_coeff_stats(sim.stdout or "")
        eob = stats["eob"]
        if not case["expect"](eob):
            fail(f"{label} {case['name']}: expected bin match, got eob={eob} stats={stats}")
        if stats["ac_nz"] <= 0:
            fail(f"{label} {case['name']}: expected non-zero AC coverage, got stats={stats}")
        check_public_decoder_case({
            "out_obu": out_obu,
            "sw_ivf": t / "encoded.ivf",
            "rtl_raw": t / "encoded_rtl_raw.obu",
            "rtl_ivf": t / "encoded_rtl.ivf",
            "recon": t / "recon.yuv",
        }, f"{label} {case['name']} qindex={case['qindex']}")
        seen.append({
            "name": case["name"],
            "pattern": case["pattern"],
            "amp": case["amp"],
            "qindex": case["qindex"],
            "eob": eob,
            "ac_nz": stats["ac_nz"],
            "max_abs": stats["max_abs"],
        })

print(f"[PASS] {label}: {seen}")
