#!/usr/bin/env python3
"""512x128 unrestricted full-coeff low-delay LAST inter proof.

This graduates the next wider 128-high full-coeff NEWMV/reference-stack
checkpoint beyond 448x128. It proves the deterministic 512x128 gradient stress
path is RTL/software byte-identical and decoder-to-recon exact without using the
testbench-only cap-disable hook.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import run_encoder_case, check_public_decoder_case, fail

W = 512
H = 128
TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=512 HEIGHT=128 all first")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural512x128_ip_fullcoeff_newmv"
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
        timeout=2_400_000_000,
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
    if globalmv != 980 or newmv != 42 or nearestmv != 2 or nearmv != 0:
        fail(
            f"expected unrestricted taller-geometry mix GLOBALMV=980 NEWMV=42 NEARESTMV=2 NEARMV=0, "
            f"saw global={globalmv} new={newmv} nearest={nearestmv} near={nearmv}"
        )
    print(
        "[PASS] 512x128 unrestricted full-coeff integer motion summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "512x128 unrestricted full-coeff integer motion")
    print("[PASS] 512x128 unrestricted full-coeff integer motion public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
