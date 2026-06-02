#!/usr/bin/env python3
"""Positive and negative RTL-byte-ownership probes for the AV1 top testbench.

The positive case proves a small all-key stream using the shared public-decoder
helper. Negative cases deliberately damage required RTL-owned artifacts and pass
only when the helper fails closed instead of falling back to software bytes or a
repaired stream.
"""
from __future__ import annotations

from pathlib import Path
import os
import argparse
import json
import shutil
import struct

from av1_public_decode import artifact_dir, fail, public_decode_proof, run

TB = Path(__file__).resolve().parent
SIM = Path(os.environ["AV1_TOP_SIM"]) if "AV1_TOP_SIM" in os.environ else TB / "Vav1_encoder_top"
W = H = 8


def write_probe(path: Path) -> None:
    y = bytes([128] * (W * H))
    cb = bytes([112] * ((W // 2) * (H // 2)))
    cr = bytes([144] * ((W // 2) * (H // 2)))
    path.write_bytes(y + cb + cr)


def fixture_paths(t: Path) -> dict[str, Path]:
    return {
        "oracle_obu": t / "encoded.obu",
        "rtl_raw": t / "encoded_rtl_raw.obu",
        "sw_ivf": t / "encoded.ivf",
        "rtl_ivf": t / "encoded_rtl.ivf",
        "recon": t / "recon.yuv",
    }


def produce_fixture(t: Path) -> dict[str, Path]:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=8 HEIGHT=8 all first")
    yuv = t / "delta8.yuv"
    write_probe(yuv)
    out_obu = t / "encoded.obu"
    run([
        SIM,
        "+frames=1",
        "+qindex=128",
        "+dc_only=1",
        "+all_key=1",
        f"+input={yuv}",
        f"+output={out_obu}",
    ])
    return fixture_paths(t)


def prove(t: Path, label: str, *, compare_ivf: bool = True) -> None:
    p = fixture_paths(t)
    manifest = public_decode_proof(
        output_dir=t,
        oracle_obu=p["oracle_obu"],
        rtl_raw_obu=p["rtl_raw"],
        sw_ivf=p["sw_ivf"],
        rtl_ivf=p["rtl_ivf"],
        recon_yuv=p["recon"],
        label=label,
        compare_ivf=compare_ivf,
    )
    written = json.loads((t / "public_decode_proof.json").read_text())
    required_decoders = {"ffmpeg/libdav1d", "aomdec"}
    if {step.get("decoder") for step in manifest.get("proof_steps", [])} != required_decoders:
        fail(f"{label}: missing decoder proof-step manifest entries")
    if set(manifest.get("decoder_tools", {})) != {"ffmpeg", "aomdec"}:
        fail(f"{label}: missing decoder tool-version manifest entries")
    if written.get("proof_steps") != manifest.get("proof_steps"):
        fail(f"{label}: persisted proof manifest lost decoder proof steps")


def clone_fixture(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def expect_fail(name: str, fn) -> None:
    try:
        fn()
    except SystemExit as exc:
        if exc.code in (0, None):
            fail(f"{name}: helper exited success; expected failure")
        print(f"[PASS] {name}: ownership helper failed closed as expected")
        return
    fail(f"{name}: unexpectedly passed")


def read_ivf_payload(ivf: Path) -> tuple[bytes, bytes, bytes]:
    data = ivf.read_bytes()
    if len(data) < 44 or data[:4] != b"DKIF":
        fail(f"not a single-frame IVF file: {ivf}")
    header = data[:32]
    frame_hdr = data[32:44]
    frame_size = struct.unpack_from("<I", frame_hdr, 0)[0]
    payload = data[44:44 + frame_size]
    if len(payload) != frame_size:
        fail(f"truncated IVF payload: {ivf}")
    return header, frame_hdr, payload


def write_ivf_with_payload(src_ivf: Path, dst_ivf: Path, payload: bytes) -> None:
    header, frame_hdr, _ = read_ivf_payload(src_ivf)
    new_frame_hdr = struct.pack("<I", len(payload)) + frame_hdr[4:12]
    dst_ivf.write_bytes(header + new_frame_hdr + payload)


def corrupt_header(payload: bytes) -> bytes:
    if not payload:
        fail("cannot corrupt empty payload")
    b = bytearray(payload)
    b[0] = 0x00
    return bytes(b)


def corrupt_obu_size(payload: bytes) -> bytes:
    if len(payload) < 3:
        fail("payload too short for OBU-size corruption")
    b = bytearray(payload)
    b[1] = 0x7F
    return bytes(b)


def mutate_raw(path: Path, mutator) -> bytes:
    data = mutator(path.read_bytes())
    path.write_bytes(data)
    return data


def run_positive(src: Path) -> None:
    prove(src, "rtl-byte-owner positive 8x8 all-key")


def negative_missing_rtl_raw(src: Path, work: Path) -> None:
    clone_fixture(src, work)
    (work / "encoded_rtl_raw.obu").unlink()
    expect_fail("N1 missing RTL raw does not fall back to software OBU", lambda: prove(work, "N1 missing RTL raw"))


def negative_testbench_repair(src: Path, work: Path) -> None:
    clone_fixture(src, work)
    mutate_raw(work / "encoded_rtl_raw.obu", corrupt_header)
    expect_fail("N2 poisoned RTL raw is not repaired/replaced", lambda: prove(work, "N2 poisoned RTL raw"))


def negative_decoder_source(src: Path, work: Path) -> None:
    clone_fixture(src, work)
    _, _, payload = read_ivf_payload(work / "encoded_rtl.ivf")
    bad_payload = corrupt_header(payload)
    write_ivf_with_payload(work / "encoded_rtl.ivf", work / "encoded_rtl.ivf", bad_payload)
    # Disable strict IVF equality here so the failure must come from decoding the
    # RTL IVF, not from the software IVF that remains valid.
    expect_fail("N3 public decoder reads RTL IVF, not software IVF", lambda: prove(work, "N3 RTL IVF poison", compare_ivf=False))


def negative_invalid_shared_oracle(src: Path, work: Path) -> None:
    clone_fixture(src, work)
    bad_payload = mutate_raw(work / "encoded.obu", corrupt_header)
    (work / "encoded_rtl_raw.obu").write_bytes(bad_payload)
    write_ivf_with_payload(work / "encoded.ivf", work / "encoded.ivf", bad_payload)
    write_ivf_with_payload(work / "encoded_rtl.ivf", work / "encoded_rtl.ivf", bad_payload)
    expect_fail("N4 matching invalid SW/RTL bytes still require public decoder validity", lambda: prove(work, "N4 invalid shared oracle"))


def negative_obu_length_repair(src: Path, work: Path) -> None:
    clone_fixture(src, work)
    bad_payload = mutate_raw(work / "encoded.obu", corrupt_obu_size)
    (work / "encoded_rtl_raw.obu").write_bytes(bad_payload)
    write_ivf_with_payload(work / "encoded.ivf", work / "encoded.ivf", bad_payload)
    write_ivf_with_payload(work / "encoded_rtl.ivf", work / "encoded_rtl.ivf", bad_payload)
    expect_fail("N5 corrupted OBU size/backpatch is not repaired by packaging", lambda: prove(work, "N5 OBU length poison"))


NEGATIVES = {
    "negative-missing-rtl-raw": negative_missing_rtl_raw,
    "negative-testbench-repair": negative_testbench_repair,
    "negative-decoder-source": negative_decoder_source,
    "negative-invalid-shared-oracle": negative_invalid_shared_oracle,
    "negative-obu-length-repair": negative_obu_length_repair,
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("case", nargs="?", default="all", choices=["positive", "all", *NEGATIVES.keys()])
    args = ap.parse_args()

    with artifact_dir("rtl_byte_owner") as root:
        src = root / "source"
        src.mkdir(parents=True, exist_ok=True)
        produce_fixture(src)
        if args.case in ("positive", "all"):
            run_positive(src)
        if args.case == "all":
            for name, fn in NEGATIVES.items():
                fn(src, root / name)
        elif args.case in NEGATIVES:
            NEGATIVES[args.case](src, root / args.case)
    print(f"[PASS] rtl-byte-owner-check case={args.case}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
