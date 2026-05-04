#!/usr/bin/env python3
"""16x16 multi-block non-zero chroma public-decoder proof."""
from pathlib import Path

from av1_public_decode import artifact_dir, fail, public_decode_proof, run

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 16


if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=16 HEIGHT=16 all first")

with artifact_dir("nonzero_chroma16_syntax") as t:
    yuv = t / "delta16.yuv"
    out_obu = t / "encoded.obu"
    yuv.write_bytes(
        bytes([128] * (W * H))
        + bytes([112] * ((W // 2) * (H // 2)))
        + bytes([144] * ((W // 2) * (H // 2)))
    )
    run([SIM, "+frames=1", "+qindex=128", "+dc_only=1", "+all_key=1", f"+input={yuv}", f"+output={out_obu}"])
    public_decode_proof(
        output_dir=t,
        oracle_obu=out_obu,
        rtl_raw_obu=t / "encoded_rtl_raw.obu",
        sw_ivf=t / "encoded.ivf",
        rtl_ivf=t / "encoded_rtl.ivf",
        recon_yuv=t / "recon.yuv",
        label="16x16 non-zero Cb/Cr TX_4X4 syntax/recon proof",
    )
print("[PASS] 16x16 non-zero Cb/Cr TX_4X4 syntax/recon proof")
