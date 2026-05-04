#!/usr/bin/env python3
"""Dynamic non-zero chroma syntax proof."""
from pathlib import Path
import os

from av1_public_decode import artifact_dir, fail, public_decode_proof, run

TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
W = H = 8


if not SIM.exists():
    fail(f"missing simulator {SIM}; run make WIDTH=8 HEIGHT=8 all first")

with artifact_dir("nonzero_chroma_syntax") as t:
    yuv = t / "delta8.yuv"
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
        label="non-zero Cb/Cr TX_4X4 syntax proof",
    )
print("[PASS] non-zero Cb/Cr TX_4X4 syntax proof")
