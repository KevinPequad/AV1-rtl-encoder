#!/usr/bin/env python3
"""Deterministic RTL packaging guard for the current reduced AV1 stream."""
from pathlib import Path
import tempfile

from av1_syntax_test_common import (
    check_public_decoder_case,
    current_width_height,
    fail,
    require,
    run_encoder_case,
    sha256,
)


def compare_hash(a: Path, b: Path, label: str) -> None:
    ha, hb = sha256(a), sha256(b)
    if ha != hb:
        fail(f"{label}: hash drift {a}={ha} {b}={hb}")
    print(f"[PASS] {label}: sha256={ha}")


require("ffmpeg", "aomdec")
W, H = current_width_height(16, 16)

with tempfile.TemporaryDirectory(prefix="av1_deterministic_packaging_") as td:
    root = Path(td)
    run_a = root / "run_a"
    run_b = root / "run_b"

    paths_a = run_encoder_case(
        run_a,
        W,
        H,
        frames=2,
        qindex=128,
        all_key=False,
        pattern="flat",
        repeat=True,
        extra_plusargs=["+me_zero_mv_only=1"],
    )
    check_public_decoder_case(paths_a, f"deterministic {W}x{H} run_a")

    paths_b = run_encoder_case(
        run_b,
        W,
        H,
        frames=2,
        qindex=128,
        all_key=False,
        pattern="flat",
        repeat=True,
        extra_plusargs=["+me_zero_mv_only=1"],
    )
    check_public_decoder_case(paths_b, f"deterministic {W}x{H} run_b")

    compare_hash(paths_a["rtl_raw"], paths_b["rtl_raw"], "RTL raw OBU deterministic across identical runs")
    compare_hash(paths_a["rtl_ivf"], paths_b["rtl_ivf"], "RTL IVF deterministic across identical runs")
    compare_hash(paths_a["recon"], paths_b["recon"], "RTL recon deterministic across identical runs")
    compare_hash(paths_a["out_obu"], paths_b["out_obu"], "software oracle OBU deterministic across identical runs")
    compare_hash(paths_a["sw_ivf"], paths_b["sw_ivf"], "software oracle IVF deterministic across identical runs")

print(f"[PASS] deterministic packaging guard {W}x{H}")
