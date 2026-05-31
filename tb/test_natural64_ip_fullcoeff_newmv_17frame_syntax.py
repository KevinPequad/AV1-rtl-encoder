#!/usr/bin/env python3
"""17-frame 64x64 full-coeff low-delay LAST unrestricted NEWMV proof.

This extends the strict full-coeff NEWMV public-decoder/recon parity checkpoint
past the 16-frame LAST-chain gate without raising the unrestricted NEWMV cap.  The
reduced reference-stack/MV-prediction path stays public-decoder exact through one
more LAST refresh: frame 16 settles to a 12 NEWMV / 1 NEARESTMV mix while RTL and
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
    out = artifact_root / "natural64_ip_fullcoeff_newmv_17frame"
    paths = run_encoder_case(
        out,
        W,
        H,
        frames=17,
        qindex=128,
        all_key=False,
        gop_mode="lowdelay_last",
        key_interval=17,
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
        10: (64, 1, 0, 52, 1, 0, 11),
        11: (64, 1, 0, 53, 1, 0, 10),
        12: (64, 1, 0, 49, 3, 0, 12),
        13: (64, 0, 0, 56, 0, 0, 8),
        14: (64, 0, 0, 54, 1, 0, 9),
        15: (64, 1, 0, 51, 0, 0, 13),
        16: (64, 0, 0, 51, 1, 0, 12),
    }
    for frame, expected in expected_summaries.items():
        got = _summary_tuple(log, frame)
        if got != expected:
            fail(f"unexpected frame-{frame} unrestricted inter signature: {got} expected={expected}")

    check_public_decoder_case(paths, "17-frame 64x64 full-coeff unrestricted NEWMV LAST-chain")

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
        "6bf32b81165f14855d1ac54a9454c66d",
        "c99f0fe997a1735f70a3080517de08c2",
        "21d38f956cedf6d646e4b8e4d24f9beb",
        "a49644d79ff861f079035aa7dcb21c2b",
        "41379f8e8a1a2baaa494755f8eea936d",
        "aff10477a17d521dd3730f458c569858",
        "5c933762a0268aa32b6f9197be78c57c",
    ]
    for label, path in (
        ("FFmpeg/libdav1d", out / "ff_rtl.yuv"),
        ("aomdec", out / "aom_rtl.yuv"),
        ("RTL recon", Path(paths["recon"])),
    ):
        got = _frame_md5s(path)
        if got != expected_md5s:
            fail(f"{label} 17-frame MD5 scope drifted: {got}")

    print(
        "[PASS] 17-frame 64x64 full-coeff unrestricted NEWMV LAST-chain: "
        "RTL/software bytes match, FFmpeg/libdav1d and aomdec decode cleanly, "
        "decoder output is byte-identical to RTL recon, and frame-16 stays at "
        "GLOBALMV=51 NEARESTMV=1 NEARMV=0 NEWMV=12"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
