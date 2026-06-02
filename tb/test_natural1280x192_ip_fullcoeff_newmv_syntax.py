#!/usr/bin/env python3
"""1280x192 full-coeff low-delay LAST inter proof with unrestricted integer motion.

This extends the taller-than-128 NEWMV/reference-stack public-clean lane beyond
1216x192. It proves the 1280x192 rectangular multi-superblock gradient stress
path is RTL/software byte-identical and public-decoder-to-recon exact without
treating cap growth as feature-complete closure.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import run_encoder_case, check_public_decoder_case, fail

W = 1280
H = 192
TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=1280 HEIGHT=192 all first")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural1280x192_ip_fullcoeff_newmv"
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
        timeout=6_200_000_000,
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
    if globalmv != 3796 or newmv != 42 or nearestmv != 2 or nearmv != 0:
        fail(
            f"expected 1280x192 unrestricted mix GLOBALMV=3796 NEWMV=42 NEARESTMV=2 NEARMV=0, "
            f"saw global={globalmv} new={newmv} nearest={nearestmv} near={nearmv}"
        )
    print(
        "[PASS] 1280x192 full-coeff unrestricted integer motion summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "1280x192 full-coeff unrestricted integer motion")
    print("[PASS] 1280x192 full-coeff unrestricted integer motion public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
