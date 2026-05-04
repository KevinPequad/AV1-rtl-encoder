#!/usr/bin/env python3
"""32x32 two-frame zero-MV fallback proof with explicit Cb/Cr residual coverage."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 32


def run(cmd, *, cwd=TB, check=True):
    print("[RUN]", " ".join(map(str, cmd)), flush=True)
    proc = subprocess.Popen(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines = []
    assert proc.stdout is not None
    for line in proc.stdout:
        lines.append(line)
        print(line, end="", flush=True)
    rc = proc.wait()
    stdout = "".join(lines)
    if check and rc != 0:
        raise SystemExit(rc)
    return subprocess.CompletedProcess(cmd, rc, stdout=stdout)


def require(name):
    if shutil.which(name) is None:
        raise SystemExit(f"missing required tool: {name}")


def fail(msg: str):
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def cmp_file(a: Path, b: Path, label: str):
    if a.read_bytes() != b.read_bytes():
        fail(f"{label}: {a} != {b} (sizes {a.stat().st_size} vs {b.stat().st_size})")
    print(f"[PASS] {label}")


def clip8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def base_planes():
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            y[yy * W + xx] = clip8(96 + 3 * xx + 4 * yy + ((xx * yy) >> 4))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = clip8(112 + 2 * xx + 2 * yy + ((xx * yy) % 8))
            cr[yy * cw + xx] = clip8(148 - xx + yy + ((xx * 3 + yy * 5) % 8))
    return bytes(y), bytes(cb), bytes(cr)


def chroma_delta(src: bytes, w: int, h: int, *, cb_plane: bool) -> bytes:
    out = bytearray(w * h)
    for yy in range(h):
        for xx in range(w):
            v = src[yy * w + xx]
            if cb_plane:
                delta = 14 if ((xx // 2 + yy // 2) & 1) == 0 else -10
                delta += (xx + yy) & 1
            else:
                delta = -12 if ((xx // 2 + 2 * (yy // 2)) & 1) == 0 else 16
                delta -= (xx ^ yy) & 1
            out[yy * w + xx] = clip8(v + delta)
    return bytes(out)


def write_probe(path: Path):
    y0, cb0, cr0 = base_planes()
    y1 = y0
    cb1 = chroma_delta(cb0, W // 2, H // 2, cb_plane=True)
    cr1 = chroma_delta(cr0, W // 2, H // 2, cb_plane=False)
    path.write_bytes(y0 + cb0 + cr0 + y1 + cb1 + cr1)


def parse_chroma_summary(log: str, frame: int):
    pat = re.compile(
        rf"chroma_summary frame={frame} "
        r"cb_nonzero_blocks=(\d+) cr_nonzero_blocks=(\d+) "
        r"cb_nonzero_coeffs=(\d+) cr_nonzero_coeffs=(\d+) "
        r"cb_inter_nonzero_blocks=(\d+) cr_inter_nonzero_blocks=(\d+) "
        r"chroma_only_blocks=(\d+) "
        r"inter_prev_cb_reads=(\d+) inter_prev_cr_reads=(\d+) "
        r"neigh_cb_reads=(\d+) neigh_cr_reads=(\d+)"
    )
    m = pat.search(log)
    if not m:
        fail(f"missing frame {frame} chroma summary")
    keys = [
        "cb_nonzero_blocks", "cr_nonzero_blocks",
        "cb_nonzero_coeffs", "cr_nonzero_coeffs",
        "cb_inter_nonzero_blocks", "cr_inter_nonzero_blocks",
        "chroma_only_blocks", "inter_prev_cb_reads", "inter_prev_cr_reads",
        "neigh_cb_reads", "neigh_cr_reads",
    ]
    return {k: int(v) for k, v in zip(keys, m.groups())}


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with tempfile.TemporaryDirectory(prefix="av1_natural32_ip_chroma_delta_") as td:
    t = Path(td)
    yuv = t / "natural32_chroma_delta_2f.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([
        str(SIM), "+frames=2", "+qindex=128", "+dc_only=1", "+all_key=0",
        "+me_zero_mv_only=1", "+dump_inter_summary=1", "+dump_chroma_summary=1",
        f"+input={yuv}", f"+output={out_obu}",
    ])
    log = sim.stdout or ""
    if "[TB] Frame 0 (KEY)" not in log:
        fail("frame 0 was not encoded as KEY")
    if "[TB] Frame 1 (INTER)" not in log:
        fail("frame 1 was not encoded as INTER")
    summary = re.search(r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+)", log)
    if not summary:
        fail("missing frame 1 inter summary")
    total_inter = int(summary.group(1))
    nonzero_inter = int(summary.group(2))
    if total_inter != 16:
        fail(f"expected 16 inter 8x8 blocks in frame 1, saw {total_inter}")
    if nonzero_inter <= 0:
        fail("expected at least one non-zero inter residual block")
    mv_lines = re.findall(r"inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\)", log)
    if len(mv_lines) != 16:
        fail(f"expected 16 frame-1 MV lines, saw {len(mv_lines)}")
    bad_mvs = [(int(b), int(x), int(y)) for b, x, y in mv_lines if int(x) != 0 or int(y) != 0]
    if bad_mvs:
        fail(f"expected all frame-1 MVs to be zero, saw {bad_mvs[:4]}")

    stats = parse_chroma_summary(log, 1)
    if stats["cb_nonzero_blocks"] <= 0 or stats["cr_nonzero_blocks"] <= 0:
        fail(f"expected non-zero frame-1 Cb and Cr blocks, saw {stats}")
    if stats["cb_nonzero_coeffs"] <= 0 or stats["cr_nonzero_coeffs"] <= 0:
        fail(f"expected non-zero frame-1 Cb and Cr coefficients, saw {stats}")
    if stats["cb_inter_nonzero_blocks"] <= 0 or stats["cr_inter_nonzero_blocks"] <= 0:
        fail(f"expected inter Cb and Cr residual blocks, saw {stats}")
    if stats["inter_prev_cb_reads"] <= 0 or stats["inter_prev_cr_reads"] <= 0:
        fail(f"expected previous-frame Cb/Cr inter prediction reads, saw {stats}")
    if stats["neigh_cb_reads"] != 0 or stats["neigh_cr_reads"] != 0:
        fail(f"frame-1 INTER chroma prediction unexpectedly used current-frame neighbor reads, saw {stats}")
    print(f"[PASS] frame-1 32x32 chroma inter exercise counts: {stats}")

    rtl_obu = t / "encoded_rtl_raw.obu"
    rtl_ivf = t / "encoded_rtl.ivf"
    sw_ivf = t / "encoded.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_sw = t / "aom_sw.yuv"
    aom_rtl = t / "aom_rtl.yuv"

    cmp_file(out_obu, rtl_obu, "concatenated RTL raw OBU matches software oracle OBU")
    cmp_file(sw_ivf, rtl_ivf, "RTL IVF matches software IVF")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_sw), str(sw_ivf)])
    cmp_file(aom_sw, recon, "aomdec software IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] 32x32 zero-MV fallback P6 chroma-inter residual proof")
