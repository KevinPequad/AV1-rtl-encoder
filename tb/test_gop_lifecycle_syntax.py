#!/usr/bin/env python3
"""64x64 13-frame low-delay LAST-only GOP lifecycle public-decoder proof."""
from pathlib import Path

from av1_public_decode import artifact_dir
from av1_syntax_test_common import (
    assert_lowdelay_last_summary,
    check_public_decoder_case,
    run_encoder_case,
)

TB = Path(__file__).resolve().parent
W = H = 64


with artifact_dir("gop_lifecycle_64x64") as t:
    paths = run_encoder_case(
        t,
        W,
        H,
        frames=13,
        qindex=128,
        all_key=False,
        gop_mode="lowdelay_last",
        key_interval=12,
        refresh_policy="last_only",
        dump_ref_summary=True,
        pattern="flat",
        repeat=True,
        dc_only=1,
        extra_plusargs=["+me_zero_mv_only=1"],
    )
    log = paths["log"]
    assert_lowdelay_last_summary(log, frame_count=13, key_interval=12, label="64x64 13-frame GOP lifecycle")
    check_public_decoder_case(paths, "64x64 13-frame GOP lifecycle")

print("[PASS] 64x64 13-frame lowdelay_last GOP lifecycle proof")
