#!/usr/bin/env python3
"""10-frame 64x64 full-coeff low-delay LAST unrestricted NEWMV proof.

This extends the strict full-coeff NEWMV public-decoder/recon parity checkpoint
past the 9-frame LAST-chain gate.  The reduced reference-stack/MV-prediction path
stays public-decoder exact through one more LAST refresh without raising the
NEWMV cap: frame 9 settles to an 11 NEWMV / 2 NEARESTMV mix while RTL and
software bytes remain identical.
"""
from pathlib import Path
import hashlib
import os
import re

from av1_syntax_test_common import check_public_decoder_case, fail, run_encoder_case

W = H = 64
TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"


def _frame_md5s(path: Path) -> list[str]:
    data = path.read_bytes()
    frame_size = W * H * 3 // 2
    if len(data) % frame_size:
        fail(f"{path}: decoded YUV size {len(data)} is not an integer number of 64x64 yuv420p frames")
    return [hashlib.md5(data[i:i + frame_size]).hexdigest() for i in range(0, len(data), frame_size)]


def _summary_tuple(log: str, frame: int) -> tuple[int, int, int, int, int, int, int]:
    match = re.search(
        rf"inter_summary frame={frame} total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        rf"mode_counts=\{{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}}",
        log,
    )
    if not match:
        fail(f"missing frame-{frame} inter summary")
    return tuple(map(int, match.groups()))


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")

    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_10frame"
    paths = run_encoder_case(
        out,
        W,
        H,
        frames=10,
        qindex=128,
        all_key=False,
        gop_mode="lowdelay_last",
        key_interval=12,
        pattern="gradient",
        repeat=False,
        dc_only=0,
        timeout=1_000_000_000,
        extra_plusargs=["+me_newmv_limit=255", "+dump_inter_summary=1", "+dump_chroma_summary=1"],
    )
    log = paths["log"]

    expected_summaries = {
        1: (64, 12, 0, 18, 3, 0, 43),
        2: (64, 1, 0, 34, 3, 0, 27),
        3: (64, 1, 0, 44, 2, 0, 18),
        4: (64, 1, 0, 51, 0, 0, 13),
        5: (64, 1, 0, 49, 0, 0, 15),
        6: (64, 1, 0, 50, 0, 0, 14),
        7: (64, 1, 0, 49, 2, 0, 13),
        8: (64, 1, 0, 47, 1, 0, 16),
        9: (64, 1, 0, 51, 2, 0, 11),
    }
    for frame, expected in expected_summaries.items():
        got = _summary_tuple(log, frame)
        if got != expected:
            fail(f"unexpected frame-{frame} unrestricted inter signature: {got} expected={expected}")

    check_public_decoder_case(paths, "10-frame 64x64 full-coeff unrestricted NEWMV LAST-chain")

    expected_md5s = [
        "18d641716080f96bc5c780d6cbfecd7a",
        "418cd2b1d3bf2c1337c2535e02aeb264",
        "6cbdc2e2d69392a4806c28e65be25d83",
        "2ae36fffe6e9d4b0faef2f225e0d776b",
        "1d654f650748d7b55cb20d1974cfd718",
        "0c8618fc924faca7e07ee036848d8565",
        "1367fd2659cb84d01cd97fe20aa64985",
        "f0d9c4559e950f941a02217d8db9598b",
        "bbc1eaa8cd23f7fb142f04ace87e20f8",
        "6ab31739bda0c7a870ba38ad2313bdf0",
    ]
    for label, path in (
        ("FFmpeg/libdav1d", out / "ff_rtl.yuv"),
        ("aomdec", out / "aom_rtl.yuv"),
        ("RTL recon", Path(paths["recon"])),
    ):
        got = _frame_md5s(path)
        if got != expected_md5s:
            fail(f"{label} 10-frame MD5 scope drifted: {got}")

    print(
        "[PASS] 10-frame 64x64 full-coeff unrestricted NEWMV LAST-chain: "
        "RTL/software bytes match, FFmpeg/libdav1d and aomdec decode cleanly, "
        "decoder output is byte-identical to RTL recon, and frame-9 stays at "
        "GLOBALMV=51 NEARESTMV=2 NEARMV=0 NEWMV=11"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
