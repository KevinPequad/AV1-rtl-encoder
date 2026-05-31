#!/usr/bin/env python3
"""Probe the next unrestricted 64x64 full-coeff NEWMV boundary.

This is intentionally not a public-clean proof.  The default RTL path remains
capped at the verified 44 non-zero-motion blocks.  This probe requests exactly one block past that cap
through the testbench-only bypass hook and pins the current decoder/recon blocker for
the first additional NEWMV block so the next fix has a reproducible target.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import run, run_encoder_case, check_rtl_ownership, require, fail

W = H = 64
TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
FRAME_SIZE = W * H * 3 // 2
FIRST_BLOCK52_Y_OFFSET = FRAME_SIZE + 48 * W + 32


def mismatch_summary(left: bytes, right: bytes) -> tuple[int, int | None, int | None, int | None]:
    first = next((i for i, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), None)
    diff_count = sum(1 for i in range(min(len(left), len(right))) if left[i] != right[i]) + abs(len(left) - len(right))
    if first is None:
        return diff_count, None, None, None
    return diff_count, first, left[first], right[first]


def assert_decoder_mismatch(decoder_yuv: Path, recon: Path, label: str) -> None:
    left = decoder_yuv.read_bytes()
    right = recon.read_bytes()
    diff_count, first, left_byte, right_byte = mismatch_summary(left, right)
    if diff_count == 0 or first is None:
        fail(f"{label}: boundary probe unexpectedly matched RTL recon; raise/replace the default cap only after proving this path")
    if first != FIRST_BLOCK52_Y_OFFSET or left_byte != 0xFE or right_byte != 0xFF:
        fail(
            f"{label}: expected first mismatch at frame1 Y block52 offset {FIRST_BLOCK52_Y_OFFSET} "
            f"with decoder=0xfe RTL=0xff, saw offset={first} decoder={left_byte!r} rtl={right_byte!r} diff_count={diff_count}"
        )
    print(
        f"[PASS] {label}: pinned expected boundary mismatch diff_count={diff_count} "
        f"first_offset={first} frame=1 plane=Y x=32 y=48 block8=52 decoder=0x{left_byte:02x} rtl=0x{right_byte:02x}"
    )


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")
    require("ffmpeg", "aomdec")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_boundary45_probe"
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
        extra_plusargs=["+me_newmv_limit=45", "+disable_fullcoeff_cap=1", "+dump_inter_summary=1"],
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
    print(f"[PASS] boundary45 summary pinned: GLOBALMV={globalmv} NEARESTMV={nearestmv} NEARMV={nearmv} NEWMV={newmv}")
    check_rtl_ownership(paths, "64x64 full-coeff boundary45 probe")
    rtl_ivf = Path(paths["rtl_ivf"])
    recon = Path(paths["recon"])
    ff_rtl = out / "ff_rtl.yuv"
    aom_rtl = out / "aom_rtl.yuv"
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", rtl_ivf, "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_rtl])
    assert_decoder_mismatch(ff_rtl, recon, "FFmpeg/libdav1d boundary45 decode vs RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", aom_rtl, rtl_ivf])
    assert_decoder_mismatch(aom_rtl, recon, "aomdec boundary45 decode vs RTL recon")
    print("[PASS] boundary45 probe remains an expected-fail public-decode diagnostic; default cap must stay at 44")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
