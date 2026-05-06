#!/usr/bin/env python3
"""Shared helpers for RTL-owned AV1 syntax/public-decoder gates."""
from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Dict, Iterable, List, Optional

TB = Path(__file__).resolve().parent
_SIM_ENV = os.environ.get("AV1_TOP_SIM")
if _SIM_ENV:
    _sim_path = Path(_SIM_ENV)
    SIM = _sim_path if _sim_path.is_absolute() else (TB / _sim_path).resolve()
else:
    SIM = TB / "Vav1_encoder_top"


def run(cmd: Iterable[object], *, cwd: Path = TB, check: bool = True) -> subprocess.CompletedProcess[str]:
    cmd_list = [str(c) for c in cmd]
    print("[RUN]", " ".join(cmd_list))
    res = subprocess.run(cmd_list, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.stdout:
        print(res.stdout, end="")
    if check and res.returncode != 0:
        raise SystemExit(res.returncode)
    return res


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def require(*names: str) -> None:
    for name in names:
        if shutil.which(name) is None:
            fail(f"missing required tool: {name}")


def require_sim() -> None:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=<w> HEIGHT=<h> all first")


def cmp_file(a: Path, b: Path, label: str) -> None:
    if not a.exists():
        fail(f"{label}: missing {a}")
    if not b.exists():
        fail(f"{label}: missing {b}")
    if a.read_bytes() != b.read_bytes():
        fail(f"{label}: {a} != {b} (sizes {a.stat().st_size} vs {b.stat().st_size})")
    print(f"[PASS] {label}")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def current_width_height(default_w: int = 32, default_h: int = 32) -> tuple[int, int]:
    return int(os.environ.get("WIDTH", default_w)), int(os.environ.get("HEIGHT", default_h))


def _clip8(v: int) -> int:
    return max(0, min(255, v))


def make_gradient_frame(width: int, height: int, *, phase: int = 0, flat: bool = False) -> bytes:
    if flat:
        return bytes([128]) * (width * height + (width // 2) * (height // 2) * 2)

    y = bytearray(width * height)
    for yy in range(height):
        for xx in range(width):
            y[yy * width + xx] = _clip8(96 + 3 * xx + 4 * yy + ((xx * yy) >> 4) + phase)

    cw, ch = width // 2, height // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = _clip8(112 + 2 * xx + 2 * yy + ((xx * yy) & 7) + phase)
            cr[yy * cw + xx] = _clip8(148 - xx + yy + ((xx * 3 + yy * 5) & 7) - phase)
    return bytes(y) + bytes(cb) + bytes(cr)


def write_yuv420(path: Path, width: int, height: int, frames: int, *, pattern: str = "gradient", repeat: bool = True) -> None:
    flat = pattern == "flat"
    data = bytearray()
    for frame in range(frames):
        phase = 0 if repeat else frame
        data.extend(make_gradient_frame(width, height, phase=phase, flat=flat))
    path.write_bytes(bytes(data))


def run_encoder_case(out_dir: Path, width: int, height: int, *, frames: int, qindex: int,
                     all_key: bool, gop_mode: Optional[str] = None, key_interval: Optional[int] = None,
                     refresh_policy: Optional[str] = None, dump_ref_summary: bool = False,
                     pattern: str = "gradient", repeat: bool = True,
                     dc_only: int = 1, extra_plusargs: Optional[List[str]] = None,
                     timeout: int = 500000000) -> Dict[str, object]:
    out_dir.mkdir(parents=True, exist_ok=True)
    yuv = out_dir / "input.yuv"
    out_obu = out_dir / "encoded.obu"
    write_yuv420(yuv, width, height, frames, pattern=pattern, repeat=repeat)
    cmd: List[object] = [
        SIM,
        f"+frames={frames}",
        f"+timeout={timeout}",
        f"+qindex={qindex}",
        f"+dc_only={dc_only}",
        f"+all_key={1 if all_key else 0}",
        f"+input={yuv}",
        f"+output={out_obu}",
        *([f"+gop_mode={gop_mode}"] if gop_mode is not None else []),
        *([f"+key_interval={key_interval}"] if key_interval is not None else []),
        *([f"+refresh_policy={refresh_policy}"] if refresh_policy is not None else []),
        *(["+dump_ref_summary=1"] if dump_ref_summary else []),
    ]
    if extra_plusargs:
        cmd.extend(extra_plusargs)
    sim = run(cmd)
    return {
        "log": sim.stdout or "",
        "out_obu": out_obu,
        "sw_ivf": out_dir / "encoded.ivf",
        "rtl_raw": out_dir / "encoded_rtl_raw.obu",
        "rtl_ivf": out_dir / "encoded_rtl.ivf",
        "recon": out_dir / "recon.yuv",
        "rtl_frames_dir": out_dir / "rtl_frames",
        "still_frames_dir": out_dir / "still_frames",
    }


def check_rtl_ownership(paths: Dict[str, object], label: str) -> None:
    cmp_file(paths["out_obu"], paths["rtl_raw"], f"{label}: RTL raw OBU matches software oracle OBU")
    cmp_file(paths["sw_ivf"], paths["rtl_ivf"], f"{label}: RTL IVF matches software oracle IVF")


def check_public_decoders(paths: Dict[str, object], label: str) -> None:
    require("ffmpeg", "aomdec")
    rtl_ivf = Path(paths["rtl_ivf"])
    recon = Path(paths["recon"])
    base = rtl_ivf.parent
    ff_rtl = base / "ff_rtl.yuv"
    aom_rtl = base / "aom_rtl.yuv"
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", rtl_ivf,
         "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_rtl])
    cmp_file(ff_rtl, recon, f"{label}: FFmpeg/libdav1d RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", aom_rtl, rtl_ivf])
    cmp_file(aom_rtl, recon, f"{label}: aomdec RTL IVF decode matches RTL recon")


def check_public_decoder_case(paths: Dict[str, object], label: str) -> None:
    check_rtl_ownership(paths, label)
    check_public_decoders(paths, label)


def assert_ip_summary(log: str, width: int, height: int, label: str) -> None:
    if "[TB] Frame 0 (KEY)" not in log:
        fail(f"{label}: frame 0 was not encoded as KEY")
    if "[TB] Frame 1 (INTER)" not in log:
        fail(f"{label}: frame 1 was not encoded as INTER")
    summary = re.search(r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+)", log)
    if not summary:
        fail(f"{label}: missing frame 1 inter summary")
    total_inter = int(summary.group(1))
    expected_blocks = (width // 8) * (height // 8)
    if total_inter != expected_blocks:
        fail(f"{label}: expected {expected_blocks} inter blocks in frame 1, saw {total_inter}")
    mv_lines = re.findall(r"inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\)", log)
    if len(mv_lines) != expected_blocks:
        fail(f"{label}: expected {expected_blocks} frame-1 MV lines, saw {len(mv_lines)}")
    bad_mvs = [(int(b), int(x), int(y)) for b, x, y in mv_lines if int(x) != 0 or int(y) != 0]
    if bad_mvs:
        fail(f"{label}: expected zero-MV inter path, saw {bad_mvs[:4]}")
    print(f"[PASS] {label}: IP summary total_inter={total_inter} zero-MV")


def assert_lowdelay_last_summary(log: str, frame_count: int, key_interval: int, label: str) -> None:
    summary_re = re.compile(r"\[TB\] ref_summary frame=(\d+) mode=(KEY|INTER) gop_mode=([a-z_]+) key_interval=(\d+) gop_pos=(\d+) frame_num=(\d+) source_ref=(NONE|LAST) refresh=0x([0-9a-fA-F]{2}) last_ref_rd=LAST last_ref_wr=LAST ref_map=0,0,0,0,0,0,0")
    summaries = {}
    for match in summary_re.finditer(log):
        frame = int(match.group(1))
        summaries[frame] = {
            "mode": match.group(2),
            "gop_mode": match.group(3),
            "key_interval": int(match.group(4)),
            "gop_pos": int(match.group(5)),
            "frame_num": int(match.group(6)),
            "source_ref": match.group(7),
            "refresh": match.group(8).lower(),
        }
    if len(summaries) != frame_count:
        fail(f"{label}: expected {frame_count} frame summaries, saw {len(summaries)}")
    for frame in range(frame_count):
        summary = summaries.get(frame)
        if summary is None:
            fail(f"{label}: missing summary for frame {frame}")
        expect_key = (frame % key_interval) == 0
        expect_mode = "KEY" if expect_key else "INTER"
        if summary["mode"] != expect_mode:
            fail(f"{label}: frame {frame} expected mode {expect_mode}, saw {summary['mode']}")
        if summary["gop_mode"] != "lowdelay_last":
            fail(f"{label}: frame {frame} expected gop_mode=lowdelay_last, saw {summary['gop_mode']}")
        if summary["key_interval"] != key_interval:
            fail(f"{label}: frame {frame} expected key_interval={key_interval}, saw {summary['key_interval']}")
        expected_gop_pos = 0 if expect_key else frame % key_interval
        if summary["gop_pos"] != expected_gop_pos:
            fail(f"{label}: frame {frame} expected gop_pos={expected_gop_pos}, saw {summary['gop_pos']}")
        expected_frame_num = 0 if expect_key else (frame % key_interval) & 0xF
        if summary["frame_num"] != expected_frame_num:
            fail(f"{label}: frame {frame} expected frame_num={expected_frame_num}, saw {summary['frame_num']}")
        expected_source_ref = "NONE" if expect_key else "LAST"
        if summary["source_ref"] != expected_source_ref:
            fail(f"{label}: frame {frame} expected source_ref={expected_source_ref}, saw {summary['source_ref']}")
        expected_refresh = "ff" if expect_key else "01"
        if summary["refresh"] != expected_refresh:
            fail(f"{label}: frame {frame} expected refresh={expected_refresh}, saw {summary['refresh']}")
    print(f"[PASS] {label}: lowdelay_last summary verified for {frame_count} frames")


class BitReader:
    def __init__(self, payload: bytes):
        self.payload = payload
        self.bit = 0

    def read_bit(self) -> int:
        if self.bit >= len(self.payload) * 8:
            fail("bitreader overrun while parsing reduced AV1 frame header")
        byte = self.payload[self.bit >> 3]
        val = (byte >> (7 - (self.bit & 7))) & 1
        self.bit += 1
        return val

    def read_bits(self, nbits: int) -> int:
        val = 0
        for _ in range(nbits):
            val = (val << 1) | self.read_bit()
        return val


def _tile_log2(blk_size: int, target: int) -> int:
    k = 0
    while (blk_size << k) < target:
        k += 1
    return k


def _skip_reduced_tile_info(br: BitReader, width: int, height: int) -> None:
    mi_cols_aligned = ((width // 4) + 15) & ~15
    mi_rows_aligned = ((height // 4) + 15) & ~15
    sb_cols = mi_cols_aligned >> 4
    sb_rows = mi_rows_aligned >> 4
    min_log2_tile_cols = _tile_log2(64, sb_cols)
    max_log2_tile_cols = _tile_log2(1, min(sb_cols, 64))
    max_log2_tile_rows = _tile_log2(1, min(sb_rows, 64))
    min_log2_tiles = max(_tile_log2(576, sb_cols * sb_rows), min_log2_tile_cols)

    uniform_tile_spacing_flag = br.read_bit()
    if uniform_tile_spacing_flag != 1:
        fail("expected uniform_tile_spacing_flag=1 in reduced frame header")
    tile_cols_log2 = min_log2_tile_cols
    if tile_cols_log2 < max_log2_tile_cols:
        br.read_bit()
    min_log2_tile_rows = max(0, min_log2_tiles - tile_cols_log2)
    tile_rows_log2 = min_log2_tile_rows
    if tile_rows_log2 < max_log2_tile_rows:
        br.read_bit()


def _parse_obus(data: bytes) -> List[tuple[int, bytes]]:
    pos = 0
    obus: List[tuple[int, bytes]] = []
    while pos < len(data):
        header = data[pos]
        pos += 1
        obu_type = (header >> 3) & 0x0F
        has_extension = (header >> 2) & 1
        has_size = (header >> 1) & 1
        if has_extension:
            pos += 1
        if not has_size:
            fail("reduced test parser expected all OBUs to carry size fields")
        size = 0
        shift = 0
        while True:
            if pos >= len(data):
                fail("truncated OBU size field")
            b = data[pos]
            pos += 1
            size |= (b & 0x7F) << shift
            if (b & 0x80) == 0:
                break
            shift += 7
        payload = data[pos:pos + size]
        if len(payload) != size:
            fail("truncated OBU payload")
        pos += size
        obus.append((obu_type, payload))
    return obus


def extract_first_frame_base_q_idx(raw_obu: Path, width: int, height: int) -> int:
    data = raw_obu.read_bytes()
    for obu_type, payload in _parse_obus(data):
        if obu_type != 6:  # OBU_FRAME
            continue
        br = BitReader(payload)
        show_existing_frame = br.read_bit()
        if show_existing_frame:
            fail("unexpected show_existing_frame=1 in reduced qindex parser")
        frame_type = br.read_bits(2)
        if frame_type != 0:
            continue
        br.read_bit()  # show_frame
        br.read_bit()  # disable_cdf_update
        br.read_bit()  # allow_screen_content_tools
        br.read_bit()  # frame_size_override_flag
        br.read_bit()  # render_and_frame_size_different
        _skip_reduced_tile_info(br, width, height)
        return br.read_bits(8)
    fail("did not find a key-frame OBU_FRAME payload while parsing qindex")
    return -1


def assert_top_level_base_q_idx(raw_obu: Path, width: int, height: int, expected_qindex: int, label: str) -> None:
    actual = extract_first_frame_base_q_idx(raw_obu, width, height)
    if actual != expected_qindex:
        fail(f"{label}: expected emitted base_q_idx={expected_qindex}, saw {actual}")
    print(f"[PASS] {label}: emitted base_q_idx={actual}")
