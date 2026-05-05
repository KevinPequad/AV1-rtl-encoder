#!/usr/bin/env python3
"""64x64 two-frame LAST-path mode/context proof for GLOBALMV/NEARESTMV/NEARMV/NEWMV.

Key trick: synthesize frame 1 from the actual reconstructed keyframe produced by the
RTL/testbench at the same qindex/coefficient mode. Rather than relying on sub-pixel
content inside a block, frame 1 remaps a few top-row 8x8 blocks to exact source blocks
from the reconstructed LAST frame. That keeps the proof RTL-owned and makes the target
motion relationships unambiguous:
- blk0: GLOBALMV / self-copy / zero motion
- blk1: NEWMV by copying blk0 into blk1
- blk2: NEWMV by copying blk0 into blk2
- blk3: NEARESTMV by copying blk1 into blk3 (same MV as blk2)
- blk4: NEARMV by copying blk3 into blk4 (same MV as blk1)
- later blocks: GLOBALMV via self-copy

Ownership rule: the testbench may compare against the software writer oracle,
but public decoder compatibility must come from the RTL raw OBU and IVF bytes
without repair, padding, or backpatching by this script.
"""
from pathlib import Path
import hashlib
import re
import shutil
import subprocess

TB = Path(__file__).resolve().parent
REPO = TB.parent
SIM = TB / "Vav1_encoder_top"
W = H = 64
BLOCK = 8
LUMA_SIZE = W * H
CHROMA_SIZE = W * H // 4
FRAME_SIZE = W * H * 3 // 2
EXPECTED_INTER_BLOCKS = (W // BLOCK) * (H // BLOCK)
SOURCE_BLOCK_BY_BLOCK = (0, 0, 0, 1, 3, 5, 6, 7)
DATA = REPO / "data" / "natural_motion64_x640_y360_2f_mode_ctx.yuv"
PREP_DATA = REPO / "data" / "natural_motion64_x640_y360_1f_mode_ctx_prep.yuv"
OUTDIR = REPO / "output" / "natural_motion64_x640_y360_2f_mode_ctx"
PREP_OUTDIR = REPO / "output" / "natural_motion64_x640_y360_1f_mode_ctx_prep"


def run(cmd, *, cwd=TB, check=True):
    print("[RUN]", " ".join(map(str, cmd)))
    res = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.stdout:
        print(res.stdout, end="")
    if check and res.returncode != 0:
        raise SystemExit(res.returncode)
    return res


def require(name):
    if shutil.which(name) is None:
        raise SystemExit(f"missing required tool: {name}")


def fail(msg: str):
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def cmp_file(a: Path, b: Path, label: str):
    if a.read_bytes() != b.read_bytes():
        fail(f"{label}: {a} != {b} (sizes {a.stat().st_size} vs {b.stat().st_size})")
    print(f"[PASS] {label} sha256={hashlib.sha256(a.read_bytes()).hexdigest()}")


def cmp_decoded(decoded: Path, recon: Path, label: str):
    dec = decoded.read_bytes()
    rec = recon.read_bytes()
    if dec != rec:
        for idx, (d, r) in enumerate(zip(dec, rec)):
            if d != r:
                fail(f"{label}: first mismatch byte={idx} frame={idx // FRAME_SIZE} plane_off={idx % FRAME_SIZE} decoded={d} recon={r}")
        fail(f"{label}: size mismatch {len(dec)} vs {len(rec)}")
    print(f"[PASS] {label} sha256={hashlib.sha256(dec).hexdigest()}")


def base_source_luma() -> bytes:
    out = bytearray([128] * LUMA_SIZE)
    # Keep the keyframe reconstruction parity-clean by limiting the nontrivial
    # structure to the top 8 luma rows. This still gives the frame-1 top row
    # enough horizontal variation to drive GLOBALMV/NEWMV/NEARESTMV/NEARMV
    # decisions via the blockwise shifted copy fixture below.
    for yy in range(8):
        row = yy * W
        for xx in range(W):
            out[row + xx] = clip8(96 + 3 * xx + 2 * yy)
    return bytes(out)

