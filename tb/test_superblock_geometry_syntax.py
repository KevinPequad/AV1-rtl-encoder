#!/usr/bin/env python3
"""Generated superblock/geometry syntax public-decoder proof."""
from pathlib import Path
import tempfile

from av1_syntax_test_common import (
    assert_ip_summary,
    check_public_decoder_case,
    current_width_height,
    require,
    run_encoder_case,
)


require("ffmpeg", "aomdec")
W, H = current_width_height(32, 32)

with tempfile.TemporaryDirectory(prefix=f"av1_geometry_{W}x{H}_") as td:
    root = Path(td)

    all_key = run_encoder_case(
        root / "all_key",
        W,
        H,
        frames=1,
        qindex=128,
        all_key=True,
        pattern="flat",
        repeat=True,
    )
    check_public_decoder_case(all_key, f"geometry {W}x{H} all-key")

    ip = run_encoder_case(
        root / "ip_zero_mv",
        W,
        H,
        frames=2,
        qindex=128,
        all_key=False,
        pattern="flat",
        repeat=True,
        extra_plusargs=["+me_zero_mv_only=1", "+dump_inter_summary=1"],
    )
    assert_ip_summary(str(ip["log"]), W, H, f"geometry {W}x{H} two-frame IP")
    check_public_decoder_case(ip, f"geometry {W}x{H} two-frame IP")

print(f"[PASS] superblock/geometry syntax public-decoder proof {W}x{H}")
