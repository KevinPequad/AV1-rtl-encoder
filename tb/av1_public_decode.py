#!/usr/bin/env python3
"""Shared AV1 public-decoder and RTL-byte-ownership proof helpers.

These helpers deliberately fail closed. A public-decoder proof may use the C++
writer as an oracle/fixture generator, but the RTL raw OBU and RTL IVF must be
present and must be the bytes decoded by FFmpeg/libdav1d and aomdec.
"""
from __future__ import annotations

from contextlib import contextmanager
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from typing import Iterable, Mapping

TB = Path(__file__).resolve().parent


def fail(message: str) -> None:
    print(f"[FAIL] {message}")
    raise SystemExit(1)


def require(tool: str) -> None:
    if shutil.which(tool) is None:
        fail(f"missing required tool: {tool}")


def run(cmd: Iterable[object], *, cwd: Path | str = TB, check: bool = True) -> subprocess.CompletedProcess:
    argv = [str(x) for x in cmd]
    print("[RUN]", " ".join(argv))
    res = subprocess.run(
        argv,
        cwd=str(cwd),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if res.stdout:
        print(res.stdout, end="")
    if check and res.returncode != 0:
        raise SystemExit(res.returncode)
    return res


def _sanitize(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", name.strip())
    return cleaned.strip("._") or "artifact"


@contextmanager
def artifact_dir(name: str):
    """Return a temporary dir, or a persistent AV1_ARTIFACT_ROOT/name dir."""
    root = os.environ.get("AV1_ARTIFACT_ROOT")
    if root:
        path = Path(root).resolve() / _sanitize(name)
        if path.exists():
            shutil.rmtree(path)
        path.mkdir(parents=True, exist_ok=True)
        yield path
    else:
        with tempfile.TemporaryDirectory(prefix=f"{_sanitize(name)}_") as td:
            yield Path(td)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def assert_present_nonempty(path: Path, label: str) -> None:
    if not path.exists():
        fail(f"{label} missing: {path}")
    if not path.is_file():
        fail(f"{label} is not a file: {path}")
    if path.stat().st_size <= 0:
        fail(f"{label} is empty: {path}")


def _yuv420_mismatch_location(offset: int, width: int, height: int) -> str:
    if width <= 0 or height <= 0 or (width % 2) or (height % 2):
        return ""
    y_size = width * height
    cw = width // 2
    ch = height // 2
    c_size = cw * ch
    frame_size = y_size + 2 * c_size
    if frame_size <= 0:
        return ""
    frame = offset // frame_size
    in_frame = offset % frame_size
    if in_frame < y_size:
        plane = "Y"
        plane_off = in_frame
        x = plane_off % width
        y = plane_off // width
        block = (y // 8) * (width // 8) + (x // 8)
        return f" yuv420=frame:{frame} plane:{plane} x:{x} y:{y} block8:{block}"
    if in_frame < y_size + c_size:
        plane = "Cb"
        plane_off = in_frame - y_size
    else:
        plane = "Cr"
        plane_off = in_frame - y_size - c_size
    x = plane_off % cw
    y = plane_off // cw
    luma_block = ((y * 2) // 8) * (width // 8) + ((x * 2) // 8)
    return f" yuv420=frame:{frame} plane:{plane} x:{x} y:{y} luma_block8:{luma_block}"


def _byte_mismatch_summary(left: bytes, right: bytes, *, yuv420: tuple[int, int] | None = None) -> str:
    first = next((i for i, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), None)
    overlap = min(len(left), len(right))
    diff_count = sum(1 for i in range(overlap) if left[i] != right[i]) + abs(len(left) - len(right))
    if first is None:
        return f"byte_mismatches={diff_count} first_mismatch=EOF"
    location = ""
    if yuv420 is not None:
        location = _yuv420_mismatch_location(first, int(yuv420[0]), int(yuv420[1]))
    return (
        f"byte_mismatches={diff_count} first_mismatch_offset={first} "
        f"left=0x{left[first]:02x} right=0x{right[first]:02x}{location}"
    )


def cmp_file(a: Path, b: Path, label: str, *, yuv420: tuple[int, int] | None = None) -> None:
    assert_present_nonempty(a, f"{label} left input")
    assert_present_nonempty(b, f"{label} right input")
    left = a.read_bytes()
    right = b.read_bytes()
    if left != right:
        detail = _byte_mismatch_summary(left, right, yuv420=yuv420)
        fail(f"{label}: {a} != {b} (sizes {len(left)} vs {len(right)}; {detail})")
    print(f"[PASS] {label}")


def _file_record(path: Path) -> dict[str, object]:
    return {
        "path": str(path),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def write_json(path: Path, data: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def public_decode_proof(
    *,
    output_dir: Path,
    oracle_obu: Path,
    rtl_raw_obu: Path,
    sw_ivf: Path,
    rtl_ivf: Path,
    recon_yuv: Path,
    label: str,
    compare_ivf: bool = True,
    width: int | None = None,
    height: int | None = None,
) -> dict[str, object]:
    """Prove RTL-owned bytes using raw equality, IVF equality, and decoders.

    The proof fails if any required artifact is absent. When compare_ivf=True,
    the software-oracle IVF and RTL IVF must be byte-identical; this catches
    syntax rewriting/repair in the packaging path. FFmpeg/libdav1d and aomdec
    are always run on the RTL IVF, and decoded output must match recon_yuv.
    """
    output_dir = Path(output_dir)
    oracle_obu = Path(oracle_obu)
    rtl_raw_obu = Path(rtl_raw_obu)
    sw_ivf = Path(sw_ivf)
    rtl_ivf = Path(rtl_ivf)
    recon_yuv = Path(recon_yuv)

    require("ffmpeg")
    require("aomdec")

    started = time.time()
    yuv420 = (int(width), int(height)) if width is not None and height is not None else None
    for path, desc in [
        (oracle_obu, "software/oracle raw OBU"),
        (rtl_raw_obu, "RTL raw OBU"),
        (sw_ivf, "software/oracle IVF"),
        (rtl_ivf, "RTL IVF"),
        (recon_yuv, "RTL reconstruction YUV"),
    ]:
        assert_present_nonempty(path, desc)

    cmp_file(oracle_obu, rtl_raw_obu, "RTL raw OBU matches software oracle OBU")
    if compare_ivf:
        cmp_file(sw_ivf, rtl_ivf, "RTL IVF matches software oracle IVF packaging")

    ff_sw = output_dir / "ffmpeg_sw.yuv"
    ff_rtl = output_dir / "ffmpeg_rtl.yuv"
    aom_rtl = output_dir / "aomdec_rtl.yuv"

    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-i", sw_ivf,
        "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_sw,
    ])
    cmp_file(ff_sw, recon_yuv, "FFmpeg software IVF decode matches RTL recon", yuv420=yuv420)

    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-i", rtl_ivf,
        "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_rtl,
    ])
    cmp_file(ff_rtl, recon_yuv, "FFmpeg RTL IVF decode matches RTL recon", yuv420=yuv420)

    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", aom_rtl, rtl_ivf])
    cmp_file(aom_rtl, recon_yuv, "aomdec RTL IVF decode matches RTL recon", yuv420=yuv420)

    files = {
        "oracle_obu": oracle_obu,
        "rtl_raw_obu": rtl_raw_obu,
        "sw_ivf": sw_ivf,
        "rtl_ivf": rtl_ivf,
        "recon_yuv": recon_yuv,
        "ffmpeg_sw_yuv": ff_sw,
        "ffmpeg_rtl_yuv": ff_rtl,
        "aomdec_rtl_yuv": aom_rtl,
    }
    manifest = {
        "label": label,
        "status": "pass",
        "compare_ivf": compare_ivf,
        "elapsed_seconds": round(time.time() - started, 3),
        "files": {name: _file_record(path) for name, path in files.items()},
    }
    write_json(output_dir / "public_decode_proof.json", manifest)
    print(f"[PASS] {label}: public decoder and RTL byte ownership proof")
    return manifest
