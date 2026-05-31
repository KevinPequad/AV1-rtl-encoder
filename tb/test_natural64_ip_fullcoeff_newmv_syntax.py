#!/usr/bin/env python3
"""64x64 full-coeff low-delay LAST unrestricted integer-motion proof.

The 64x64 reduced reference-stack/MV-prediction model is public-decoder exact
for the natural full-coeff fixture with unrestricted requested motion
(+me_newmv_limit=255): block 52 is admitted as NEWMV and the neighboring
candidate-stack hits stay decoder/recon clean without a block-local guard.
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
    nonzero_motion_modes = newmv + nearestmv + nearmv
    if nonzero_motion_modes != 46:
        fail(
            f"expected 64x64 full-coeff unrestricted request to admit 46 non-zero motion-mode blocks, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    if newmv != 43 or nearestmv != 3 or nearmv != 0:
        fail(
            f"expected unrestricted mix NEWMV=43 NEARESTMV=3 NEARMV=0, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    block52 = re.search(
        r"inter_summary frame=1 blk=52 [^\n]* ref=\((-?\d+),(-?\d+)\) "
        r"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
        log,
    )
    if not block52:
        fail("missing unrestricted block 52 summary")
    ref_x, ref_y, near_x, near_y = map(int, block52.groups()[:4])
    if block52.group(5) != "NEWMV":
        fail("expected unrestricted block 52 to be admitted as NEWMV")
    if (ref_x, ref_y, near_x, near_y) != (128, -128, 0, 0):
        fail(
            "unrestricted block 52 reference-MV selection drifted: "
            f"ref=({ref_x},{ref_y}) near=({near_x},{near_y})"
        )
    block52_line = block52.group(0)
    for expected in (
        "cand0=(64,-128,w=644)",
        "cand1=(128,-128,w=648)",
        "cand2=(0,0,w=648)",
    ):
        if expected not in block52_line:
            fail(
                "unrestricted block 52 candidate stack drifted: "
                f"missing {expected}; line={block52_line}"
            )

    block58 = re.search(
        r"inter_summary frame=1 blk=58 [^\n]* ref=\((-?\d+),(-?\d+)\) "
        r"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
        log,
    )
    if not block58 or block58.group(5) != "NEWMV" or tuple(map(int, block58.groups()[:2])) != (0, 0):
        fail("expected downstream block58 to remain NEWMV/ref=(0,0)")
    block60 = re.search(
        r"inter_summary frame=1 blk=60 [^\n]* ref=\((-?\d+),(-?\d+)\) "
        r"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
        log,
    )
    if not block60 or block60.group(5) != "NEARESTMV" or tuple(map(int, block60.groups()[:2])) != (64, -128):
        fail("expected downstream block60 to be NEARESTMV/ref=(64,-128)")
    print(
        "[PASS] 64x64 full-coeff unrestricted-request integer motion summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "64x64 full-coeff unrestricted-request integer motion")
    print("[PASS] 64x64 full-coeff unrestricted-request integer motion public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
