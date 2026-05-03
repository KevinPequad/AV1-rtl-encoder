#!/usr/bin/env python3
"""Static guard for top-level chroma residual integration.

This intentionally checks ownership/capture plumbing only. Full non-zero
TX_4X4 chroma coefficient syntax is a later gate; this guard prevents the top
level from silently regressing to predictor-only chroma reconstruction.
"""
from pathlib import Path
import re

repo = Path(__file__).resolve().parents[1]
top = (repo / "rtl" / "av1_encoder_top.v").read_text()
tb = (repo / "tb" / "tb_av1_encoder.cpp").read_text()
writer = (repo / "tb" / "av1_bitstream_writer.h").read_text()

checks = [
    ("top instantiates av1_chroma_residual", r"av1_chroma_residual\s+u_chroma_residual"),
    ("top has explicit chroma residual wait state", r"TS_CHR_RES_WAIT"),
    ("top starts chroma residual core", r"chroma_res_start\s*<=\s*1'b1"),
    ("top exposes captured Cb qcoeff", r"chr_cb_qcoeff\s*\[0:15\]"),
    ("top exposes captured Cr qcoeff", r"chr_cr_qcoeff\s*\[0:15\]"),
    ("writer BlockInfo carries Cb qcoeff", r"int16_t\s+cb_qcoeff\[16\]"),
    ("writer BlockInfo carries Cr qcoeff", r"int16_t\s+cr_qcoeff\[16\]"),
    ("testbench captures Cb qcoeff", r"av1_encoder_top__DOT__chr_cb_qcoeff"),
    ("testbench captures Cr qcoeff", r"av1_encoder_top__DOT__chr_cr_qcoeff"),
]

ok = True
for name, pattern in checks:
    haystack = top if name.startswith("top") else tb if name.startswith("testbench") else writer
    if not re.search(pattern, haystack):
        print(f"[FAIL] {name}: missing pattern {pattern}")
        ok = False
    else:
        print(f"[PASS] {name}")

# Stale predictor-only chroma comments are too easy to cargo-cult back into the FSM.
stale = [
    "zero-residual intra/key chroma",
    "chroma residual coding is\n                // still zero-only",
]
for s in stale:
    if s in top:
        print(f"[FAIL] stale predictor-only chroma path/comment remains: {s!r}")
        ok = False

raise SystemExit(0 if ok else 1)
