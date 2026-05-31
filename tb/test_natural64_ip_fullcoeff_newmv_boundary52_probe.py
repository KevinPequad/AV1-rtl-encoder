#!/usr/bin/env python3
"""Opt-in 64x64 full-coeff NEWMV block-52 public-decoder mismatch probe.

The default 64x64 full-coeff NEWMV path intentionally pins block 52 to
GLOBALMV while the reduced reference-stack/MV-prediction model is not yet
public-decoder exact there.  This probe opens just that guard with
+me_allow_boundary52_newmv and records the localized mismatch signature so the
next reference-stack fix can be measured without editing RTL between runs.
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


def yuv420_mismatch_stats(left: bytes, right: bytes, width: int, height: int):
    y_size = width * height
    cw = width // 2
    ch = height // 2
    c_size = cw * ch
    frame_size = y_size + 2 * c_size
    stats = {}
    for off, (a, b) in enumerate(zip(left, right)):
        if a == b:
            continue
        frame = off // frame_size
        in_frame = off % frame_size
        if in_frame < y_size:
            plane = "Y"
            plane_off = in_frame
            block = ((plane_off // width) // 8) * (width // 8) + ((plane_off % width) // 8)
        elif in_frame < y_size + c_size:
            plane = "Cb"
            plane_off = in_frame - y_size
            block = (((plane_off // cw) * 2) // 8) * (width // 8) + (((plane_off % cw) * 2) // 8)
        else:
            plane = "Cr"
            plane_off = in_frame - y_size - c_size
            block = (((plane_off // cw) * 2) // 8) * (width // 8) + (((plane_off % cw) * 2) // 8)
        key = (frame, plane, block)
        delta = int(left[off]) - int(right[off])
        entry = stats.setdefault(key, {"count": 0, "sad": 0, "min_delta": delta, "max_delta": delta, "max_abs": 0})
        entry["count"] += 1
        entry["sad"] += abs(delta)
        entry["min_delta"] = min(entry["min_delta"], delta)
        entry["max_delta"] = max(entry["max_delta"], delta)
        entry["max_abs"] = max(entry["max_abs"], abs(delta))
    if len(left) != len(right):
        stats[("size", "bytes", -1)] = {
            "count": abs(len(left) - len(right)),
            "sad": 0,
            "min_delta": 0,
            "max_delta": 0,
            "max_abs": 0,
        }
    return stats


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_boundary52_probe"
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
        extra_plusargs=[
            "+me_newmv_limit=255",
            "+me_allow_boundary52_newmv=1",
            "+dump_inter_summary=1",
        ],
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
    if (total_inter, nonzero_inter, globalmv, nearestmv, nearmv, newmv) != (64, 12, 18, 3, 0, 43):
        fail(
            "unexpected unguarded block52 mode-count signature: "
            f"total={total_inter} nonzero={nonzero_inter} "
            f"GLOBALMV={globalmv} NEARESTMV={nearestmv} NEARMV={nearmv} NEWMV={newmv}"
        )
    block52 = re.search(
        r"inter_summary frame=1 blk=52 [^\n]* ref=\((-?\d+),(-?\d+)\) "
        r"near=\((-?\d+),(-?\d+)\) mode=([A-Z]+MV) [^\n]*",
        log,
    )
    if not block52:
        fail("missing block52 unguarded summary")
    if block52.group(5) != "NEWMV" or tuple(map(int, block52.groups()[:4])) != (0, 0, 128, -128):
        fail(f"unexpected block52 unguarded stack signature: {block52.group(0)}")
    block60 = re.search(r"inter_summary frame=1 blk=60 [^\n]* mode=([A-Z]+MV) [^\n]*", log)
    if not block60 or block60.group(1) != "NEARESTMV":
        fail("expected downstream block60 to flip to NEARESTMV in the unguarded probe")

    check_rtl_ownership(paths, "64x64 full-coeff unguarded block52 probe")
    require("ffmpeg", "aomdec")
    rtl_ivf = Path(paths["rtl_ivf"])
    recon = Path(paths["recon"])
    ff_yuv = out / "ff_unguarded.yuv"
    aom_yuv = out / "aom_unguarded.yuv"
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", rtl_ivf, "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_yuv])
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", aom_yuv, rtl_ivf])
    ff = ff_yuv.read_bytes()
    aom = aom_yuv.read_bytes()
    rtl_recon = recon.read_bytes()
    if ff != aom:
        fail("FFmpeg/libdav1d and aomdec disagree on the unguarded block52 probe")
    if ff == rtl_recon:
        fail("unguarded block52 unexpectedly reached public-decoder/recon parity; update/remove this negative probe")
    expected = {
        (1, "Y", 52): {"count": 64, "sad": 64, "min_delta": -1, "max_delta": -1, "max_abs": 1},
        (1, "Cb", 52): {"count": 16, "sad": 32, "min_delta": -2, "max_delta": -2, "max_abs": 2},
        (1, "Cb", 60): {"count": 16, "sad": 112, "min_delta": 7, "max_delta": 7, "max_abs": 7},
        (1, "Cr", 52): {"count": 16, "sad": 128, "min_delta": -8, "max_delta": -8, "max_abs": 8},
        (1, "Cr", 60): {"count": 16, "sad": 32, "min_delta": -2, "max_delta": -2, "max_abs": 2},
    }
    actual = yuv420_mismatch_stats(ff, rtl_recon, W, H)
    if actual != expected:
        fail(f"unexpected public-decoder mismatch signature: {actual}")
    print(
        "[PASS] 64x64 full-coeff unguarded block52 probe: "
        "RTL/software bytes match, FFmpeg/aomdec agree, public recon mismatch remains localized "
        "to frame1 blocks 52/60"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
