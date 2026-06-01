#!/usr/bin/env python3
"""64x64 two-frame LAST-path mode/context syntax gate for GLOBALMV/NEARESTMV/NEARMV/NEWMV.

The gate is real: it first encodes a 1-frame preparatory clip to capture the
actual reconstructed LAST frame, then synthesizes frame 1 from that recon so the
top-row 8x8 blocks exercise the claimed mode chain. The testbench checks the
mode-context summary, RTL raw OBU / IVF byte ownership, and public decoder to
RTL recon parity for the reduced reference-stack/MV-prediction path.
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import os
import re

from av1_public_decode import artifact_dir, fail, run
from av1_syntax_test_common import check_public_decoders

TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
W = H = 64
BLOCK = 8
LUMA_SIZE = W * H
CHROMA_SIZE = W * H // 4
FRAME_SIZE = W * H * 3 // 2
TOP_ROW_BLOCK_MAP = (0, 0, 0, 1, 3, 5, 6, 7)
EXPECTED_BLOCKS = {
    0: ("GLOBALMV", 0, 0),
    1: ("NEWMV", -64, 0),
    2: ("NEWMV", -128, 0),
    3: ("NEARESTMV", -128, 0),
    4: ("NEARMV", -64, 0),
    5: ("GLOBALMV", 0, 0),
    6: ("GLOBALMV", 0, 0),
    7: ("GLOBALMV", 0, 0),
}


def clip8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def run_encoder(
    input_yuv: Path,
    output_obu: Path,
    *,
    frames: int,
    qindex: int,
    all_key: bool,
    dump_inter_summary: bool = False,
    timeout_cycles: int = 30_000_000,
) -> str:
    cmd = [
        SIM,
        f"+frames={frames}",
        f"+timeout={timeout_cycles}",
        f"+qindex={qindex}",
        "+dc_only=1",
        f"+all_key={1 if all_key else 0}",
        f"+input={input_yuv}",
        f"+output={output_obu}",
    ]
    if dump_inter_summary:
        cmd.append("+dump_inter_summary=1")
    result = run(cmd, cwd=TB)
    return result.stdout or ""


def cmp_file(a: Path, b: Path, label: str) -> None:
    if a.read_bytes() != b.read_bytes():
        fail(f"{label}: {a} != {b} (sizes {a.stat().st_size} vs {b.stat().st_size})")
    print(f"[PASS] {label} sha256={hashlib.sha256(a.read_bytes()).hexdigest()}")


def make_prep_frame() -> bytes:
    y = bytearray([128] * LUMA_SIZE)
    # Keep the keyframe reconstruction parity-clean by limiting the nontrivial
    # structure to the top 8x8 block row. A piecewise-constant block gradient is
    # sufficient to drive the frame-1 block copy chain while staying friendly to
    # public decoders.
    block_vals = [96, 100, 104, 108, 112, 116, 120, 124]
    for block_x, val in enumerate(block_vals):
        for yy in range(BLOCK):
            row = yy * W + block_x * BLOCK
            for xx in range(BLOCK):
                y[row + xx] = val
    cb = bytes([128] * CHROMA_SIZE)
    cr = bytes([128] * CHROMA_SIZE)
    return bytes(y) + cb + cr


def copy_luma_block(
    dst: bytearray,
    dst_x: int,
    dst_y: int,
    src: bytes,
    src_x: int,
    src_y: int,
    block_size: int = BLOCK,
) -> None:
    for yy in range(block_size):
        dst_row = (dst_y + yy) * W + dst_x
        src_row = (src_y + yy) * W + src_x
        dst[dst_row:dst_row + block_size] = src[src_row:src_row + block_size]


def build_mode_context_frame(source_recon: bytes) -> bytes:
    if len(source_recon) != FRAME_SIZE:
        fail(f"prep recon size mismatch: expected {FRAME_SIZE} bytes, got {len(source_recon)}")
    src_y = source_recon[:LUMA_SIZE]
    y = bytearray([128] * LUMA_SIZE)
    for block_x, source_block_x in enumerate(TOP_ROW_BLOCK_MAP):
        copy_luma_block(y, block_x * BLOCK, 0, src_y, source_block_x * BLOCK, 0)
    cb = bytes([128] * CHROMA_SIZE)
    cr = bytes([128] * CHROMA_SIZE)
    return bytes(y) + cb + cr


def parse_inter_summary(log: str):
    summary_re = re.compile(
        r"\[TB\] inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}"
    )
    summary = summary_re.search(log)
    if not summary:
        fail("missing frame 1 inter summary")
    total_inter, nonzero_inter, first_inter_blk, globalmv_count, nearestmv_count, nearmv_count, newmv_count = map(
        int,
        summary.groups(),
    )
    block_re = re.compile(
        r"\[TB\] inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\).*? mode=(GLOBALMV|NEARESTMV|NEARMV|NEWMV) "
    )
    blocks = {}
    for match in block_re.finditer(log):
        blk = int(match.group(1))
        mvx = int(match.group(2))
        mvy = int(match.group(3))
        mode = match.group(4)
        blocks[blk] = (mode, mvx, mvy)
    return {
        "total_inter": total_inter,
        "nonzero_inter": nonzero_inter,
        "first_inter_blk": first_inter_blk,
        "mode_counts": {
            "GLOBALMV": globalmv_count,
            "NEARESTMV": nearestmv_count,
            "NEARMV": nearmv_count,
            "NEWMV": newmv_count,
        },
        "blocks": blocks,
    }


def verify_mode_context_log(log: str) -> None:
    if "[TB] Frame 0 (KEY)" not in log:
        fail("frame 0 was not encoded as KEY")
    if "[TB] Frame 1 (INTER)" not in log:
        fail("frame 1 was not encoded as INTER")
    parsed = parse_inter_summary(log)
    counts = parsed["mode_counts"]
    for mode in ("GLOBALMV", "NEARESTMV", "NEARMV", "NEWMV"):
        if counts[mode] <= 0:
            fail(f"expected at least one {mode} block in frame 1, saw {counts[mode]}")
    for blk, expected in EXPECTED_BLOCKS.items():
        got = parsed["blocks"].get(blk)
        if got is None:
            fail(f"missing inter_summary line for block {blk}")
        if got != expected:
            fail(f"block {blk} expected {expected}, saw {got}")
    print(
        "[PASS] 64x64 LAST-path mode/context summary: "
        f"total_inter={parsed['total_inter']} nonzero_inter={parsed['nonzero_inter']} "
        f"mode_counts={counts}"
    )


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")

    prep_frame = make_prep_frame()

    with artifact_dir("natural64_ip_mode_context_prep") as prep_dir:
        prep_input = prep_dir / "prep_input.yuv"
        prep_input.write_bytes(prep_frame)
        prep_obu = prep_dir / "encoded.obu"
        run_encoder(prep_input, prep_obu, frames=1, qindex=128, all_key=True)
        prep_recon = prep_dir / "recon.yuv"
        if not prep_recon.exists():
            fail(f"prep recon missing: {prep_recon}")
        source_recon = prep_recon.read_bytes()
        if len(source_recon) != FRAME_SIZE:
            fail(f"prep recon size mismatch: expected {FRAME_SIZE} bytes, got {len(source_recon)}")
        print(f"[INFO] prep recon sha256={hashlib.sha256(source_recon).hexdigest()}")

    with artifact_dir("natural64_ip_mode_context") as out_dir:
        mode_input = out_dir / "mode_context_input.yuv"
        mode_input.write_bytes(prep_frame + build_mode_context_frame(source_recon))
        out_obu = out_dir / "encoded.obu"
        log = run_encoder(mode_input, out_obu, frames=2, qindex=128, all_key=False, dump_inter_summary=True)
        verify_mode_context_log(log)
        rtl_raw_obu = out_dir / "encoded_rtl_raw.obu"
        sw_ivf = out_dir / "encoded.ivf"
        rtl_ivf = out_dir / "encoded_rtl.ivf"
        recon_yuv = out_dir / "recon.yuv"
        if not recon_yuv.exists():
            fail(f"recon missing: {recon_yuv}")
        cmp_file(out_obu, rtl_raw_obu, "RTL raw OBU matches software oracle OBU")
        cmp_file(sw_ivf, rtl_ivf, "RTL IVF matches software oracle IVF packaging")
        check_public_decoders(
            {
                "rtl_ivf": rtl_ivf,
                "recon": recon_yuv,
                "width": W,
                "height": H,
                "log": log,
            },
            "64x64 LAST-path mode-context",
        )

    print("[PASS] 64x64 LAST-path mode-context public-decoder syntax gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
