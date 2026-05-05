#!/usr/bin/env python3
"""Strict integrity checks for RTL-owned AV1 raw OBU and IVF artifacts.

This helper intentionally treats tb/av1_bitstream_writer.h output as an oracle only.
It proves that encoded_rtl.ivf contains exactly the raw RTL OBU bytes captured from
bs_mem writes, and that the raw AV1 OBU size fields bound the whole stream.
"""
from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional

OBU_SEQUENCE_HEADER = 1
OBU_TEMPORAL_DELIMITER = 2
OBU_FRAME_HEADER = 3
OBU_TILE_GROUP = 4
OBU_FRAME = 6


@dataclass(frozen=True)
class Obu:
    obu_type: int
    header_offset: int
    payload_offset: int
    payload_size: int
    leb_size: int


def fail(msg: str) -> None:
    raise SystemExit(f"[FAIL] {msg}")


def decode_leb128(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    for idx in range(8):
        if pos + idx >= len(data):
            fail(f"truncated LEB128 at byte {pos}")
        byte = data[pos + idx]
        value |= (byte & 0x7F) << (idx * 7)
        if (byte & 0x80) == 0:
            return value, idx + 1
    fail(f"LEB128 at byte {pos} exceeds AV1 8-byte limit")
    raise AssertionError("unreachable")


def parse_obus(data: bytes, label: str, *, require_fixed_frame_leb: bool = True) -> List[Obu]:
    if not data:
        fail(f"{label}: empty raw OBU stream")
    obus: List[Obu] = []
    pos = 0
    while pos < len(data):
        header_offset = pos
        header = data[pos]
        pos += 1
        if header & 0x80:
            fail(f"{label}: obu_forbidden_bit set at byte {header_offset}")
        obu_type = (header >> 3) & 0x0F
        has_extension = (header >> 2) & 0x01
        has_size = (header >> 1) & 0x01
        if header & 0x01:
            fail(f"{label}: reserved OBU header bit set at byte {header_offset}")
        if has_extension:
            if pos >= len(data):
                fail(f"{label}: missing OBU extension byte after {header_offset}")
            pos += 1
        if not has_size:
            fail(f"{label}: OBU at byte {header_offset} lacks size field")
        payload_size, leb_size = decode_leb128(data, pos)
        pos += leb_size
        payload_offset = pos
        end = pos + payload_size
        if end > len(data):
            fail(
                f"{label}: OBU type {obu_type} at byte {header_offset} declares "
                f"{payload_size} payload bytes, beyond stream size {len(data)}"
            )
        if obu_type == OBU_TEMPORAL_DELIMITER and payload_size != 0:
            fail(f"{label}: temporal delimiter at byte {header_offset} has non-zero payload {payload_size}")
        if obu_type == OBU_SEQUENCE_HEADER and payload_size == 0:
            fail(f"{label}: sequence header at byte {header_offset} has zero payload")
        if obu_type == OBU_FRAME:
            if payload_size == 0:
                fail(f"{label}: frame OBU at byte {header_offset} has zero payload")
            if require_fixed_frame_leb and leb_size != 4:
                fail(
                    f"{label}: frame OBU at byte {header_offset} used {leb_size}-byte LEB; "
                    "current RTL ownership contract requires fixed 4-byte frame OBU sizes"
                )
        obus.append(Obu(obu_type, header_offset, payload_offset, payload_size, leb_size))
        pos = end
    if pos != len(data):
        fail(f"{label}: parser ended at {pos}, stream has {len(data)} bytes")
    if not any(o.obu_type == OBU_FRAME for o in obus):
        fail(f"{label}: no frame OBU found")
    print(f"[PASS] {label}: parsed {len(obus)} OBUs, {len(data)} bytes")
    return obus


def read_ivf_payloads(path: Path) -> List[bytes]:
    data = path.read_bytes()
    if len(data) < 32:
        fail(f"{path}: too short for IVF DKIF header")
    if data[0:4] != b"DKIF":
        fail(f"{path}: missing DKIF magic")
    header_size = struct.unpack_from("<H", data, 6)[0]
    frame_count = struct.unpack_from("<I", data, 24)[0]
    if header_size < 32 or header_size > len(data):
        fail(f"{path}: invalid IVF header size {header_size}")
    pos = header_size
    payloads: List[bytes] = []
    while pos < len(data):
        if pos + 12 > len(data):
            fail(f"{path}: truncated IVF frame header at byte {pos}")
        frame_size = struct.unpack_from("<I", data, pos)[0]
        pos += 12
        if frame_size == 0:
            fail(f"{path}: zero-length IVF frame payload")
        end = pos + frame_size
        if end > len(data):
            fail(f"{path}: IVF frame declares {frame_size} bytes beyond file size")
        payloads.append(data[pos:end])
        pos = end
    if frame_count != len(payloads):
        fail(f"{path}: IVF header frame_count={frame_count}, parsed={len(payloads)}")
    print(f"[PASS] {path}: parsed {len(payloads)} IVF payload frame(s)")
    return payloads


def sorted_frame_files(rtl_dir: Path) -> List[Path]:
    if not rtl_dir.exists():
        return []
    return sorted(rtl_dir.glob("frame_*_rtl_raw.obu"))


def check_output_dir(
    output_dir: Path | str,
    *,
    stem: str = "encoded",
    expected_frames: Optional[int] = None,
    require_fixed_frame_leb: bool = True,
) -> None:
    out = Path(output_dir)
    raw_path = out / f"{stem}_rtl_raw.obu"
    ivf_path = out / f"{stem}_rtl.ivf"
    rtl_dir = out / "rtl_frames"
    if not raw_path.exists():
        fail(f"missing concatenated RTL raw OBU: {raw_path}")
    if not ivf_path.exists():
        fail(f"missing RTL IVF: {ivf_path}")

    raw = raw_path.read_bytes()
    parse_obus(raw, str(raw_path), require_fixed_frame_leb=require_fixed_frame_leb)

    frame_files = sorted_frame_files(rtl_dir)
    if expected_frames is not None and len(frame_files) != expected_frames:
        fail(f"{rtl_dir}: expected {expected_frames} per-frame RTL raw files, saw {len(frame_files)}")
    frame_payloads: List[bytes] = []
    for idx, frame_path in enumerate(frame_files):
        frame_raw = frame_path.read_bytes()
        parse_obus(frame_raw, str(frame_path), require_fixed_frame_leb=require_fixed_frame_leb)
        frame_payloads.append(frame_raw)
        if not frame_raw:
            fail(f"{frame_path}: empty per-frame payload")
    if frame_payloads:
        concat = b"".join(frame_payloads)
        if concat != raw:
            fail(f"concatenated rtl_frames payloads do not equal {raw_path}")
        print(f"[PASS] per-frame RTL raw files concatenate exactly to {raw_path.name}")

    ivf_payloads = read_ivf_payloads(ivf_path)
    if expected_frames is not None and len(ivf_payloads) != expected_frames:
        fail(f"{ivf_path}: expected {expected_frames} IVF frames, saw {len(ivf_payloads)}")
    if frame_payloads and len(ivf_payloads) != len(frame_payloads):
        fail(f"{ivf_path}: parsed {len(ivf_payloads)} IVF payloads but {len(frame_payloads)} raw frame files")
    if frame_payloads:
        for idx, (ivf_payload, frame_payload) in enumerate(zip(ivf_payloads, frame_payloads)):
            if ivf_payload != frame_payload:
                fail(f"{ivf_path}: IVF payload frame {idx} differs from RTL raw frame file")
    if b"".join(ivf_payloads) != raw:
        fail(f"{ivf_path}: concatenated IVF payloads differ from {raw_path}")
    print("[PASS] RTL IVF payload bytes exactly equal RTL raw OBU bytes")


def main(argv: Optional[Iterable[str]] = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--stem", default="encoded")
    parser.add_argument("--frames", type=int, default=None)
    parser.add_argument("--allow-variable-frame-leb", action="store_true")
    args = parser.parse_args(list(argv) if argv is not None else None)
    check_output_dir(
        args.output_dir,
        stem=args.stem,
        expected_frames=args.frames,
        require_fixed_frame_leb=not args.allow_variable_frame_leb,
    )


if __name__ == "__main__":
    main()
