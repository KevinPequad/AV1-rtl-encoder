#!/usr/bin/env python3
"""64x64 full-coeff low-delay LAST inter proof with capped integer NEWMV.

The 64x64 reduced reference-stack/MV-prediction model is public-decoder exact
through the first 26 requested NEWMV blocks. Larger explicit NEWMV requests are
capped in RTL for full-coeff 64x64+ paths until the unrestricted reference-stack
model is decoder-exact.
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
    out = TB / "artifacts" / "natural64_ip_fullcoeff_newmv"
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
        extra_plusargs=["+me_newmv_limit=255", "+dump_inter_summary=1"],
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
    if total_inter != (W // 8) * (H // 8):
        fail(f"expected all blocks inter in frame 1, saw {total_inter}")
    if nonzero_inter <= 0:
        fail("expected nonzero full-coeff inter residual blocks")
    if newmv != 26:
        fail(f"expected 64x64 full-coeff NEWMV cap to hold at 26 blocks, saw {newmv}")
    if nearestmv != 0 or nearmv != 0:
        fail(f"capped full-coeff path should avoid NEAREST/NEARMV stack parity risk, saw nearest={nearestmv} near={nearmv}")
    print(
        "[PASS] 64x64 full-coeff capped integer NEWMV summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "64x64 full-coeff capped integer NEWMV")
    print("[PASS] 64x64 full-coeff capped integer NEWMV public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
