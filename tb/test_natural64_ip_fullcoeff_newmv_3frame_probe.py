#!/usr/bin/env python3
"""Expected-fail diagnostic for the 3-frame 64x64 full-coeff NEWMV widening.

The 2-frame unrestricted 64x64 full-coeff NEWMV proof is public-decoder clean.
This probe widens the same fixture to 3 low-delay-LAST frames and keeps the
current blocker executable: RTL/software bytes still match, while both public
decoders reconstruct frame-2 Cb two samples one LSB above RTL recon. Passing
this test means the known mismatch is still the narrow frame-2 chroma inter
recon blocker; once fixed, this probe should be flipped to strict decoder parity.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import (
    SIM,
    check_rtl_ownership,
    fail,
    require,
    run,
    run_encoder_case,
)

W = H = 64
TB = Path(__file__).resolve().parent


def _mismatches(dec: Path, recon: Path) -> list[tuple[int, int, int]]:
    left = dec.read_bytes()
    right = recon.read_bytes()
    overlap = min(len(left), len(right))
    out = [(i, left[i], right[i]) for i in range(overlap) if left[i] != right[i]]
    if len(left) != len(right):
        out.extend((i, -1, -1) for i in range(overlap, max(len(left), len(right))))
    return out


def _check_expected_decoder_delta(dec: Path, recon: Path, label: str) -> None:
    expected = [(16997, 0xA4, 0xA3), (17001, 0xA8, 0xA7)]
    got = _mismatches(dec, recon)
    if got != expected:
        fail(f"{label}: expected current 3-frame Cb mismatch {expected}, saw {got[:8]} count={len(got)}")
    print(
        f"[PASS] {label}: reproduced current frame-2 Cb +1 public-decoder delta "
        "at (x=5,y=19)/blk33 and (x=9,y=19)/blk34"
    )


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")
    require("ffmpeg", "aomdec")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_3frame_probe"
    paths = run_encoder_case(
        out,
        W,
        H,
        frames=3,
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
            "+dump_inter_summary=1",
            "+dump_ref_summary=1",
            "+dump_chroma_summary=1",
            "+dump_chroma_detail=1",
            "+dump_chroma_detail_start=33",
            "+dump_chroma_detail_end=34",
        ],
    )
    log = paths["log"]
    check_rtl_ownership(paths, "3-frame 64x64 full-coeff NEWMV widening probe")

    frame1 = re.search(
        r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}",
        log,
    )
    frame2 = re.search(
        r"inter_summary frame=2 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}",
        log,
    )
    if not frame1 or not frame2:
        fail("missing frame-1/frame-2 inter summaries")
    if tuple(map(int, frame1.groups())) != (64, 12, 0, 18, 3, 0, 43):
        fail(f"unexpected frame-1 unrestricted signature: {frame1.groups()}")
    if tuple(map(int, frame2.groups())) != (64, 1, 0, 34, 3, 0, 27):
        fail(f"unexpected frame-2 widened signature: {frame2.groups()}")
    for blk in (33, 34):
        if not re.search(rf"inter_summary frame=2 blk={blk} .* mode=NEWMV ", log):
            fail(f"expected frame-2 block {blk} to stay in the NEWMV region")

    expected_chroma_detail = {
        33: "inter=1 mv=(104,-128) cb_has=0 cr_has=1 cb_nz=0 cr_nz=1 "
            "cb_qcoeff=0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 "
            "cr_qcoeff=1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
        34: "inter=1 mv=(40,-128) cb_has=1 cr_has=1 cb_nz=1 cr_nz=1 "
            "cb_qcoeff=1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 "
            "cr_qcoeff=1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
    }
    for blk, expected in expected_chroma_detail.items():
        detail = re.search(rf"\[TB\] chroma_detail frame=2 blk={blk} ([^\n]+)", log)
        if not detail:
            fail(f"missing frame-2 block {blk} chroma detail")
        if detail.group(1) != expected:
            fail(f"unexpected frame-2 block {blk} chroma detail: {detail.group(1)}")
    print("[PASS] frame-2 Cb blocker blocks have stable chroma coeff/prediction signature")

    rtl_ivf = Path(paths["rtl_ivf"])
    recon = Path(paths["recon"])
    ff_rtl = out / "ff_rtl.yuv"
    aom_rtl = out / "aom_rtl.yuv"
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", rtl_ivf,
         "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_rtl])
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", aom_rtl, rtl_ivf])
    _check_expected_decoder_delta(ff_rtl, recon, "FFmpeg/libdav1d")
    _check_expected_decoder_delta(aom_rtl, recon, "aomdec")
    print(
        "[PASS] 3-frame 64x64 full-coeff NEWMV widening probe: bytes match, "
        "public decoders agree on the same narrow frame-2 Cb recon blocker"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
