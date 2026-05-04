#!/usr/bin/env python3
"""32x32 natural-ish non-zero chroma/luma public-decoder proof."""
from pathlib import Path

from av1_public_decode import artifact_dir, fail, public_decode_proof, run

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 32


def clip8(v: int) -> int:
    return max(0, min(255, v))


def write_probe(path: Path):
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            y[yy * W + xx] = clip8(96 + 3 * xx + 4 * yy + ((xx * yy) >> 4))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = clip8(112 + 2 * xx + 2 * yy + ((xx * yy) & 7))
            cr[yy * cw + xx] = clip8(148 - xx + yy + ((xx * 3 + yy * 5) & 7))
    path.write_bytes(bytes(y) + bytes(cb) + bytes(cr))


if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with artifact_dir("natural32_chroma_syntax") as t:
    yuv = t / "natural32.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    run([SIM, "+frames=1", "+qindex=128", "+dc_only=1", "+all_key=1", f"+input={yuv}", f"+output={out_obu}"])
    public_decode_proof(
        output_dir=t,
        oracle_obu=out_obu,
        rtl_raw_obu=t / "encoded_rtl_raw.obu",
        sw_ivf=t / "encoded.ivf",
        rtl_ivf=t / "encoded_rtl.ivf",
        recon_yuv=t / "recon.yuv",
        label="32x32 natural-ish luma/chroma RTL-owned proof",
    )

print("[PASS] 32x32 natural-ish luma/chroma RTL-owned proof")
