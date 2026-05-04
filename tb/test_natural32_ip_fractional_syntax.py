#!/usr/bin/env python3
"""32x32 two-frame natural-ish inter proof with small RTL-owned fractional NEWMV coverage."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import test_rtl_obu_ivf_integrity as rtl_integrity

TB = Path(__file__).resolve().parent
SIM = TB / "Vav1_encoder_top"
W = H = 32
HALFPEL_COEFF = (0, 2, -14, 76, 76, -14, 2, 0)


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
    print(f"[PASS] {label}")


def clip8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def horizontal_halfpel(src: bytes, w: int, h: int) -> bytes:
    out = bytearray(w * h)
    for yy in range(h):
        row = yy * w
        for xx in range(w):
            acc = 0
            for tap, coeff in enumerate(HALFPEL_COEFF):
                sx = min(w - 1, max(0, xx + tap - 3))
                acc += coeff * src[row + sx]
            out[row + xx] = clip8((acc + 64) >> 7)
    return bytes(out)


def base_planes():
    y = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            # Natural-ish, non-degenerate gradients in both axes with local texture so
            # half-pel interpolation is distinguishable from full-pel ties.
            y[yy * W + xx] = clip8(82 + 5 * xx + 3 * yy + ((xx * yy) >> 3) + ((3 * xx + 5 * yy) & 9))
    cw = ch = W // 2
    cb = bytearray(cw * ch)
    cr = bytearray(cw * ch)
    for yy in range(ch):
        for xx in range(cw):
            cb[yy * cw + xx] = clip8(104 + 3 * xx + 2 * yy + ((xx * yy) % 13))
            cr[yy * cw + xx] = clip8(158 - xx + 3 * yy + ((xx * 5 + yy * 3) % 11))
    return bytes(y), bytes(cb), bytes(cr)


def write_probe(path: Path):
    y0, cb0, cr0 = base_planes()
    # Frame 1 luma is synthesized at a horizontal half-pel offset from frame 0.
    # The test only accepts fractional q3 NEWMV blocks emitted by RTL-owned syntax;
    # it does not repair bytes or force the software writer to hide RTL drift.
    y1 = horizontal_halfpel(y0, W, H)
    path.write_bytes(y0 + cb0 + cr0 + y1 + cb0 + cr0)


require("ffmpeg")
require("aomdec")
if not SIM.exists():
    raise SystemExit(f"missing simulator {SIM}; run make WIDTH=32 HEIGHT=32 all first")

with tempfile.TemporaryDirectory(prefix="av1_natural32_ip_frac_") as td:
    t = Path(td)
    yuv = t / "natural32_2f_halfpel.yuv"
    out_obu = t / "encoded.obu"
    write_probe(yuv)
    sim = run([str(SIM), "+frames=2", "+qindex=128", "+dc_only=1", "+all_key=0",
               "+dump_inter_summary=1", "+me_newmv_limit=2", "+ownership_strict=1",
               f"+input={yuv}", f"+output={out_obu}"])
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
        fail(f"expected 16 inter blocks in frame 1, saw {total_inter}")
    if nonzero_inter <= 0:
        fail("expected non-zero inter residual coverage")
    mv_lines = re.findall(r"inter_summary frame=1 blk=(\d+) mv=\((-?\d+),(-?\d+)\)", log)
    if len(mv_lines) != 16:
        fail(f"expected 16 frame-1 MV lines, saw {len(mv_lines)}")
    newmv_lines = [(int(b), int(x), int(y)) for b, x, y in mv_lines if int(x) != 0 or int(y) != 0]
    if len(newmv_lines) != 2:
        fail(f"expected exactly two small NEWMV blocks under +me_newmv_limit=2, saw {newmv_lines}")
    frac_lines = [(b, x, y) for b, x, y in newmv_lines if (x % 8) != 0 or (y % 8) != 0]
    if not frac_lines:
        fail(f"expected at least one fractional q3 MV, saw only integer-q3 MVs {newmv_lines}")
    print(f"[PASS] exercised small fractional NEWMV set {newmv_lines}")

    rtl_obu = t / "encoded_rtl_raw.obu"
    rtl_ivf = t / "encoded_rtl.ivf"
    sw_ivf = t / "encoded.ivf"
    recon = t / "recon.yuv"
    ff_sw = t / "ff_sw.yuv"
    ff_rtl = t / "ff_rtl.yuv"
    aom_rtl = t / "aom_rtl.yuv"

    cmp_file(out_obu, rtl_obu, "concatenated RTL raw OBU matches software oracle OBU")
    rtl_integrity.check_output_dir(t, expected_frames=2)
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(sw_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_sw)])
    cmp_file(ff_sw, recon, "FFmpeg software IVF decode matches RTL recon")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(rtl_ivf),
         "-f", "rawvideo", "-pix_fmt", "yuv420p", str(ff_rtl)])
    cmp_file(ff_rtl, recon, "FFmpeg RTL IVF decode matches RTL recon")
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", str(aom_rtl), str(rtl_ivf)])
    cmp_file(aom_rtl, recon, "aomdec RTL IVF decode matches RTL recon")

print("[PASS] 32x32 two-frame natural-ish fractional NEWMV inter RTL-owned proof")
