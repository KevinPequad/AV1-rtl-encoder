#!/usr/bin/env python3
"""Static guard for the P4 lossy-lane quant header policy.

The current P4 implementation deliberately keeps qmatrix, delta-q, SB-QP,
and tx-mode selection disabled while broadening coverage of the existing
64x64-SB/8x8-leaf lane.  This guard protects both the software oracle and RTL
header writer from silently drifting into partially-owned syntax.
"""
from pathlib import Path
import re

TB = Path(__file__).resolve().parent
ROOT = TB.parent
writer = (TB / "av1_bitstream_writer.h").read_text()
rtl = (ROOT / "rtl" / "av1_bitstream.v").read_text()
tb = (TB / "tb_av1_encoder.cpp").read_text()

checks = []

def expect(label, cond):
    if cond:
        print(f"[PASS] {label}")
    else:
        print(f"[FAIL] {label}")
        checks.append(label)

expect("software writer emits base_q_idx from qindex_",
       "bw.write_bits(qindex_, 8);  // base_q_idx" in writer)
expect("software writer keeps all quant deltas disabled",
       all(s in writer for s in [
           "DeltaQYDc delta_coded = 0",
           "diff_uv_delta = 0",
           "DeltaQUDc delta_coded = 0",
           "DeltaQUAc delta_coded = 0",
       ]))
expect("software writer keeps using_qmatrix disabled",
       "using_qmatrix = 0" in writer)
expect("software writer keeps delta_q_present disabled in key/inter headers",
       writer.count("delta_q_present = 0") >= 2)
expect("software writer keeps tx_mode_select disabled in key/inter headers",
       writer.count("tx_mode_select = 0") >= 2)

m = re.search(r"task\s+bw_write_quantization_params;(?P<body>.*?)endtask", rtl, re.S)
expect("RTL bitstream quantization task exists", m is not None)
if m:
    body = m.group("body")
    expect("RTL header emits qidx as base_q_idx", "bw_write_bits(qidx, 8);" in body)
    expect("RTL header emits five zero quant-delta/qmatrix bits", len(re.findall(r"bw_write_bit\(0\);", body)) >= 5)
expect("RTL frame headers keep delta_q_present disabled", rtl.count("// delta_q_present") >= 2)
expect("RTL frame headers keep tx_mode_select disabled", rtl.count("// tx_mode_select") >= 2)
expect("top-level qindex=0 remains clamped until a dedicated lossless lane lands",
       "qindex      <= (qindex_in == 8'd0) ? 8'd1 : qindex_in;" in (ROOT / "rtl" / "av1_encoder_top.v").read_text()
       and "qindex <= 0 ? 1 : qindex" in tb)

if checks:
    raise SystemExit(1)
print("[PASS] P4 quant-header-disabled policy guard")
