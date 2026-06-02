#!/usr/bin/env python3
"""128x64 full-coeff low-delay LAST inter proof with unrestricted integer motion.

This is the first width-stretched geometry beyond the natural 64x64 full-coeff
NEWMV proof.  It keeps the taller-than-64 public-clean cap intact while proving
that the reduced single-reference LAST ref-stack/MV-prediction model is
public-decoder exact for the deterministic 128x64 gradient stress path.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import run_encoder_case, check_public_decoder_case, fail

W = 128
H = 64
TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=128 HEIGHT=64 all first")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural128x64_ip_fullcoeff_newmv"
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
        timeout=800_000_000,
        # Aggregate-only inter summary keeps the proof signal for this wider
        # unrestricted NEWMV gate without emitting per-block log spam.
        extra_plusargs=["+me_newmv_limit=255", "+dump_inter_summary=2"],
    )
    log = paths["log"]
    if re.search(r"inter_summary frame=1 blk=", log):
        fail("aggregate-only inter summary unexpectedly emitted per-block lines")
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
    if newmv != 52 or nearestmv != 10 or nearmv != 0:
        fail(
            f"expected width-stretched unrestricted mix NEWMV=52 NEARESTMV=10 NEARMV=0, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    print(
        "[PASS] 128x64 full-coeff unrestricted integer motion summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "128x64 full-coeff unrestricted integer motion")
    print("[PASS] 128x64 full-coeff unrestricted integer motion public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
