#!/usr/bin/env python3
"""Retired block-52 guard compatibility/public-decoder proof.

The former +me_allow_boundary52_newmv negative probe is now a positive
regression: the plusarg is accepted as an ignored legacy knob, block 52 is
admitted as NEWMV, RTL/software bytes match, and FFmpeg/libdav1d plus aomdec
match RTL recon.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import (
    SIM,
    check_public_decoder_case,
    fail,
    run_encoder_case,
)

W = H = 64
TB = Path(__file__).resolve().parent


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_boundary52_probe"
    paths = run_encoder_case(
        out,
        W,
        H,
        frames=2,
        qindex=128,
        all_key=False,
        gop_mode="lowdelay_last",
        key_interval=12,
        pattern="gradient",
        repeat=False,
        dc_only=0,
        timeout=300_000_000,
        extra_plusargs=[
            "+me_newmv_limit=255",
            "+me_allow_boundary52_newmv=1",
            "+dump_inter_summary=1",
        ],
    )
    log = paths["log"]
    summary = re.search(
        r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}",
        log,
    )
    if not summary:
        fail("missing frame-1 inter summary")
    total_inter, nonzero_inter, _first, globalmv, nearestmv, nearmv, newmv = map(int, summary.groups())
    if (total_inter, nonzero_inter, globalmv, nearestmv, nearmv, newmv) != (64, 12, 18, 3, 0, 43):
        fail(
            "unexpected unrestricted block52 mode-count signature: "
            f"total={total_inter} nonzero={nonzero_inter} "
            f"GLOBALMV={globalmv} NEARESTMV={nearestmv} NEARMV={nearmv} NEWMV={newmv}"
        )
    block52 = re.search(
        r"inter_summary frame=1 blk=52 [^\n]* ref=\((-?\d+),(-?\d+)\) "
        r"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
        log,
    )
    if not block52:
        fail("missing block52 unrestricted summary")
    if block52.group(5) != "NEWMV" or tuple(map(int, block52.groups()[:4])) != (128, -128, 0, 0):
        fail(f"unexpected block52 unrestricted stack signature: {block52.group(0)}")
    block60 = re.search(r"inter_summary frame=1 blk=60 [^\n]* mode=([A-Z]+MV) [^\n]*", log)
    if not block60 or block60.group(1) != "NEARESTMV":
        fail("expected downstream block60 to settle as NEARESTMV after the stack-order fix")

    check_public_decoder_case(paths, "64x64 full-coeff unrestricted block52 compatibility probe")
    print(
        "[PASS] 64x64 full-coeff unrestricted block52 compatibility probe: "
        "RTL/software bytes match, FFmpeg/aomdec agree, and public recon parity holds"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
