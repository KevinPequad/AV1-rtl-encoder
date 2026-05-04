#!/usr/bin/env python3
"""Fixed-QP/qindex edge syntax public-decoder proof."""
from pathlib import Path
import tempfile

from av1_syntax_test_common import (
    assert_top_level_base_q_idx,
    check_public_decoder_case,
    current_width_height,
    fail,
    require,
    run_encoder_case,
)


require("ffmpeg", "aomdec")
W, H = current_width_height(8, 8)
# Public-decoder proof is intentionally small because low qindex can make the
# current coefficient path very slow under threaded Verilator. The full qindex
# set is covered by header-qindex-sweep-check; this top-level gate proves the
# lossless-deferred qindex=0 clamp and a high-qindex edge through RTL raw bytes,
# IVF packaging, and both public decoders.
QINDICES = [0, 255]

with tempfile.TemporaryDirectory(prefix=f"av1_qindex_edges_{W}x{H}_") as td:
    root = Path(td)
    for qindex in QINDICES:
        label = f"qindex-edge {W}x{H} requested_qindex={qindex}"
        paths = run_encoder_case(
            root / f"q{qindex:03d}",
            W,
            H,
            frames=1,
            qindex=qindex,
            all_key=True,
            pattern="flat",
            repeat=True,
        )
        expected_qindex = 1 if qindex <= 0 else qindex
        if qindex <= 0 and "Requested qindex=0 clamps to qindex=1" not in str(paths["log"]):
            fail(f"{label}: missing explicit top-level qindex=0 clamp warning")
        assert_top_level_base_q_idx(paths["rtl_raw"], W, H, expected_qindex, label)
        check_public_decoder_case(paths, label)

print(f"[PASS] qindex edge syntax public-decoder proof {W}x{H} qindices={QINDICES}")
