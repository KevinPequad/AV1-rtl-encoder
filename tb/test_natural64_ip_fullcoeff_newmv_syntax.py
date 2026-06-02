#!/usr/bin/env python3
"""64x64 full-coeff low-delay LAST inter proof with unrestricted integer motion.

The 64x64 reduced reference-stack/MV-prediction model is public-decoder exact
for the current unrestricted natural full-coeff LAST-frame stress path. Larger
than 64x64 geometries remain conservatively capped until their unrestricted
reference-stack model is decoder-exact.
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
    out = artifact_root / "natural64_ip_fullcoeff_newmv"
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
        # Aggregate-only inter summary keeps the full public decode proof
        # signal while avoiding per-block log spam for wider follow-on gates.
        extra_plusargs=["+me_newmv_limit=255", "+dump_inter_summary=2"],
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
    nonzero_motion_modes = newmv + nearestmv + nearmv
    if nonzero_motion_modes != 46:
        fail(
            f"expected unrestricted 64x64 full-coeff motion to expose 46 non-zero-motion blocks, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    if newmv != 43 or nearestmv != 3 or nearmv != 0:
        fail(
            f"expected reduced stack mix NEWMV=43 NEARESTMV=3 NEARMV=0, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    print(
        "[PASS] 64x64 full-coeff unrestricted integer motion summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "64x64 full-coeff unrestricted integer motion")
    print("[PASS] 64x64 full-coeff unrestricted integer motion public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
