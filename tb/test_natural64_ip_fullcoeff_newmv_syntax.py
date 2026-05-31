#!/usr/bin/env python3
"""64x64 full-coeff low-delay LAST inter proof with block52-guarded integer motion.

The 64x64 reduced reference-stack/MV-prediction model is public-decoder exact
for the natural full-coeff fixture with unrestricted requested motion
(+me_newmv_limit=255): 43 NEWMV payload blocks plus two stack-hit NEARESTMV
blocks, while the known block-52 candidate-stack probe remains pinned to
GLOBALMV until the unrestricted reference-stack model is decoder-exact.
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
    if nonzero_motion_modes != 45:
        fail(
            f"expected 64x64 full-coeff block52-guarded unrestricted request to hold at 45 non-zero blocks, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    if newmv != 43 or nearestmv != 2 or nearmv != 0:
        fail(
            f"expected block52-guarded unrestricted mix NEWMV=43 NEARESTMV=2 NEARMV=0, "
            f"saw new={newmv} nearest={nearestmv} near={nearmv}"
        )
    block52 = re.search(
        r"inter_summary frame=1 blk=52 [^\n]* ref=\((-?\d+),(-?\d+)\) "
        r"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
        log,
    )
    if not block52:
        fail("missing cap-boundary block 52 summary")
    ref_x, ref_y, near_x, near_y = map(int, block52.groups()[:4])
    if block52.group(5) != "GLOBALMV":
        fail(
            "expected cap-boundary block 52 to stay GLOBALMV; "
            "this guarded candidate-stack probe is still the unrestricted recon-parity blocker"
        )
    if (ref_x, ref_y, near_x, near_y) != (0, 0, 128, -128):
        fail(
            "cap-boundary block 52 reference-MV selection drifted before the "
            f"known unrestricted recon fix: ref=({ref_x},{ref_y}) near=({near_x},{near_y})"
        )
    block52_line = block52.group(0)
    for expected in (
        "cand0=(64,-128,w=644)",
        "cand1=(0,0,w=648)",
        "cand2=(128,-128,w=648)",
    ):
        if expected not in block52_line:
            fail(
                "cap-boundary block 52 candidate stack drifted before the "
                f"known unrestricted recon fix: missing {expected}; line={block52_line}"
            )

    for blk, ref_pair in ((58, (0, 0)), (60, (0, 0))):
        admitted = re.search(
            rf"inter_summary frame=1 blk={blk} [^\n]* ref=\((-?\d+),(-?\d+)\) "
            rf"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
            log,
        )
        if not admitted:
            fail(f"missing guarded-cap admitted NEWMV block {blk} summary")
        blk_ref = tuple(map(int, admitted.groups()[:2]))
        if admitted.group(5) != "NEWMV" or blk_ref != ref_pair:
            fail(
                f"guarded-cap admitted block {blk} drifted: "
                f"mode={admitted.group(5)} ref={blk_ref} expected NEWMV/ref={ref_pair}"
            )
    print(
        "[PASS] 64x64 full-coeff block52-guarded unrestricted-request integer motion summary: "
        f"total_inter={total_inter} nonzero_inter={nonzero_inter} "
        f"mode_counts={{GLOBALMV:{globalmv} NEARESTMV:{nearestmv} NEARMV:{nearmv} NEWMV:{newmv}}}"
    )
    check_public_decoder_case(paths, "64x64 full-coeff block52-guarded unrestricted-request integer motion")
    print("[PASS] 64x64 full-coeff block52-guarded unrestricted-request integer motion public-decoder proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
