#!/usr/bin/env python3
"""Regression for the former 64x64 full-coeff NEWMV boundary-45 mismatch.

The previous block-52 failure came from reduced ref-MV stack ordering: top-right
was ranked before the left-column candidate, so RTL/software encoded a NEWMV
relative to the wrong nearest reference and public decoders reconstructed a
different source block. This keeps that exact boundary request pinned as a
public-clean proof while the main unrestricted 64x64 test covers the full natural
stress path.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import run_encoder_case, check_public_decoder_case, fail

W = H = 64
TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_boundary45_regression"
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
        extra_plusargs=["+me_newmv_limit=45", "+dump_inter_summary=1"],
    )
    log = paths["log"]
    summary = re.search(
        r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}",
        log,
    )
    if not summary:
        fail("missing frame-1 inter summary")
    total_inter, _nonzero_inter, _first, globalmv, nearestmv, nearmv, newmv = map(int, summary.groups())
    if total_inter != (W // 8) * (H // 8):
        fail(f"expected all blocks inter in frame 1, saw {total_inter}")
    if newmv != 43 or nearestmv != 2 or nearmv != 0:
        fail(f"expected boundary45 mix NEWMV=43 NEARESTMV=2 NEARMV=0, saw new={newmv} nearest={nearestmv} near={nearmv}")
    block52 = re.search(
        r"inter_summary frame=1 blk=52 mv=\(64,-128\) ref=\(128,-128\) near=\(0,0\) mode=NEWMV",
        log,
    )
    if not block52:
        fail("expected block52 NEWMV to use libaom-ordered ref=(128,-128) near=(0,0)")
    print(f"[PASS] boundary45 summary pinned: GLOBALMV={globalmv} NEARESTMV={nearestmv} NEARMV={nearmv} NEWMV={newmv}; block52 ref stack ordered")
    check_public_decoder_case(paths, "64x64 full-coeff boundary45 ref-stack regression")
    print("[PASS] boundary45 regression is public-decoder clean; it is no longer an expected-fail cap blocker")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
