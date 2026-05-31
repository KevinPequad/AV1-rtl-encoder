#!/usr/bin/env python3
"""Expected-fail diagnostic for the 3-frame 64x64 full-coeff NEWMV widening.

The 2-frame unrestricted 64x64 full-coeff NEWMV proof is public-decoder clean.
This probe widens the same fixture to 3 low-delay-LAST frames and keeps the
current blocker executable: RTL/software bytes still match, while both public
decoders reconstruct frame-2 Cb two samples one LSB above RTL recon. Passing
this test means the known mismatch is still the narrow frame-2 chroma inter
recon blocker; once fixed, this probe should be flipped to strict decoder parity.
"""
from pathlib import Path
import os
import re

from av1_syntax_test_common import (
    SIM,
    check_rtl_ownership,
    fail,
    require,
    run,
    run_encoder_case,
)

W = H = 64
TB = Path(__file__).resolve().parent


def _mismatches(dec: Path, recon: Path) -> list[tuple[int, int, int]]:
    left = dec.read_bytes()
    right = recon.read_bytes()
    overlap = min(len(left), len(right))
    out = [(i, left[i], right[i]) for i in range(overlap) if left[i] != right[i]]
    if len(left) != len(right):
        out.extend((i, -1, -1) for i in range(overlap, max(len(left), len(right))))
    return out


def _check_public_decoders_agree(ff_dec: Path, aom_dec: Path) -> None:
    got = _mismatches(ff_dec, aom_dec)
    if got:
        fail(f"FFmpeg/libdav1d and aomdec disagree on 3-frame blocker decode: {got[:8]} count={len(got)}")
    print("[PASS] FFmpeg/libdav1d and aomdec produce identical YUV for the 3-frame blocker stream")


def _mismatch_scope(mismatches: list[tuple[int, int, int]]) -> dict[tuple[int, str, int], int]:
    counts: dict[tuple[int, str, int], int] = {}
    y_size = W * H
    c_size = (W // 2) * (H // 2)
    frame_size = y_size + 2 * c_size
    for off, _dec_v, _recon_v in mismatches:
        frame = off // frame_size
        in_frame = off % frame_size
        if in_frame < y_size:
            block = ((in_frame // W) // 8) * (W // 8) + ((in_frame % W) // 8)
            key = (frame, "Y", block)
        elif in_frame < y_size + c_size:
            plane_off = in_frame - y_size
            block = (((plane_off // (W // 2)) * 2) // 8) * (W // 8) + (((plane_off % (W // 2)) * 2) // 8)
            key = (frame, "Cb", block)
        else:
            plane_off = in_frame - y_size - c_size
            block = (((plane_off // (W // 2)) * 2) // 8) * (W // 8) + (((plane_off % (W // 2)) * 2) // 8)
            key = (frame, "Cr", block)
        counts[key] = counts.get(key, 0) + 1
    return counts


def _check_expected_decoder_delta(dec: Path, recon: Path, label: str) -> None:
    expected = [(16997, 0xA4, 0xA3), (17001, 0xA8, 0xA7)]
    got = _mismatches(dec, recon)
    if got != expected:
        fail(f"{label}: expected current 3-frame Cb mismatch {expected}, saw {got[:8]} count={len(got)}")
    expected_scope = {(2, "Cb", 33): 1, (2, "Cb", 34): 1}
    scope = _mismatch_scope(got)
    if scope != expected_scope:
        fail(f"{label}: expected mismatch scope {expected_scope}, saw {scope}")
    print(
        f"[PASS] {label}: reproduced current frame-2 Cb +1 public-decoder delta "
        "at (x=5,y=19)/blk33 and (x=9,y=19)/blk34; Y and Cr remain decoder/recon exact"
    )


def _cb_offset(frame: int) -> int:
    return frame * (W * H * 3 // 2) + (W * H)


def _round_filter(samples: list[int], coeffs: tuple[int, ...]) -> int:
    return max(0, min(255, (sum(c * s for c, s in zip(coeffs, samples)) + 64) >> 7))


def _same_size_chroma_origin(blk: int, px: int, py: int, mvx_q3: int, mvy_q3: int) -> tuple[int, int, int, int]:
    """Return the unscaled AV1 4:2:0 chroma base sample and q4 phase.

    This mirrors libaom's unscaled `dec_calc_subpel_params` path: the chroma
    `pix_col`/`pix_row` are already in plane samples and the clamped MV is used
    directly as q4 chroma displacement.  The scaled-reference path's chroma
    siting half-sample offset is not applied for this identity-scale LAST case.
    Keeping this derivation executable prevents tempting but wrong RTL-side
    phase+1 or +8 chroma-siting workarounds: the current public-decoder +1 has
    to be explained by decoded MV/ref-stack or syntax/filter-selection behavior,
    not by changing the RTL chroma phase from 8.
    """
    blk_cols = W // 8
    cur_x = (blk % blk_cols) * 4 + px
    cur_y = (blk // blk_cols) * 4 + py
    return cur_x + (mvx_q3 >> 4), cur_y + (mvy_q3 >> 4), mvx_q3 & 15, mvy_q3 & 15


def _wrong_scaled_siting_origin(blk: int, px: int, py: int, mvx_q3: int, mvy_q3: int) -> tuple[int, int, int, int]:
    """Return the tempting scaled-path +8 chroma-siting derivation.

    This is intentionally *not* the origin used by the current unscaled LAST
    fixture; the probe records it only so the next debug pass does not spend
    another run trying the same incorrect RTL workaround.
    """
    blk_cols = W // 8
    cur_x = (blk % blk_cols) * 4 + px
    cur_y = (blk // blk_cols) * 4 + py
    x_q4 = mvx_q3 + 8
    y_q4 = mvy_q3 + 8
    return cur_x + (x_q4 >> 4), cur_y + (y_q4 >> 4), x_q4 & 15, y_q4 & 15


def _libaom_q10_unscaled_chroma_origin(blk: int, px: int, py: int, mvx_q3: int, mvy_q3: int) -> tuple[int, int, int, int]:
    """Mirror current libaom's unscaled 4:2:0 chroma subpel setup.

    `av1/common/reconinter.h:init_subpel_params()` forms q4 current-plane
    positions, multiplies by `1 << SCALE_EXTRA_BITS`, then adds
    `SCALE_EXTRA_OFF`.  For the identity-scale LAST case that offset is only a
    q10 rounding guard and must not advance the q4 filter phase.  Keeping this
    executable beside the reduced spec derivation prevents the current
    frame-2 +1 Cb delta from being "fixed" by a blind chroma phase +1.
    """
    blk_cols = W // 8
    cur_x = (blk % blk_cols) * 4 + px
    cur_y = (blk // blk_cols) * 4 + py
    scale_extra_bits = 6
    scale_extra_off = 1 << (scale_extra_bits - 1)
    pos_x_q10 = ((cur_x << 4) + mvx_q3) * (1 << scale_extra_bits) + scale_extra_off
    pos_y_q10 = ((cur_y << 4) + mvy_q3) * (1 << scale_extra_bits) + scale_extra_off
    return pos_x_q10 >> 10, pos_y_q10 >> 10, (pos_x_q10 & 1023) >> 6, (pos_y_q10 & 1023) >> 6


def _check_expected_inter_stack(log: str) -> None:
    """Pin the frame-2 blocker to the current reduced ref-MV stack.

    The public-decoder delta only appears after widening into frame 2, where
    blk33/34 are encoded as NEWMV against non-zero nearest candidates inherited
    from frame-2 neighbors.  Keep the exact ref/near/candidate-stack signature
    executable so the next fix can distinguish syntax/ref-stack drift from the
    chroma reconstruction delta itself.
    """
    expected = {
        33: "[TB] inter_summary frame=2 blk=33 mv=(104,-128) ref=(120,-88) near=(48,-56) "
            "mode=NEWMV mode_ctx=84 ctx(new=4 zero=0 ref=5) dc=0 nz=0 cand_count=6 "
            "cand0=(120,-88,w=644) cand1=(48,-56,w=644) cand2=(128,-120,w=644) "
            "cand3=(0,0,w=4) cand4=(32,-32,w=4) cand5=(-40,32,w=4)",
        34: "[TB] inter_summary frame=2 blk=34 mv=(40,-128) ref=(128,-120) near=(104,-128) "
            "mode=NEWMV mode_ctx=84 ctx(new=4 zero=0 ref=5) dc=0 nz=0 cand_count=7 "
            "cand0=(128,-120,w=644) cand1=(104,-128,w=644) cand2=(0,0,w=644) "
            "cand3=(120,-88,w=4) cand4=(48,-24,w=4) cand5=(48,-56,w=4) cand6=(-88,72,w=4)",
    }
    for blk, expected_line in expected.items():
        line = re.search(rf"\[TB\] inter_summary frame=2 blk={blk} [^\n]+", log)
        if not line:
            fail(f"missing frame-2 block {blk} inter stack summary")
        if line.group(0) != expected_line:
            fail(f"frame-2 block {blk} inter stack drifted: {line.group(0)}")
    print("[PASS] frame-2 Cb blocker MV/ref stack signature stable for blk33/34")


def _mv_class_base(mv_class: int) -> int:
    return (2 << (mv_class + 2)) if mv_class else 0


def _log2_floor(n: int) -> int:
    out = 0
    while n > 1:
        n >>= 1
        out += 1
    return out


def _mv_class(z: int) -> tuple[int, int]:
    """Mirror libaom's get_mv_class(z, &offset) helper for an absolute component minus one."""
    mv_class = 10 if z >= 2 * 4096 else _log2_floor(z >> 3)
    return mv_class, z - _mv_class_base(mv_class)


def _force_integer_mv_roundtrip(comp: int) -> int:
    """Return the q3 component decoded by AV1 force_integer_mv for this encoded comp.

    In force_integer_mv frames libaom reads no fractional symbols and reconstructs
    each coded component with fr=3/hp=1, so only q3 multiples of 8 round-trip.
    The 3-frame Cb blocker components below are all exact multiples; this keeps
    the narrowed failure from being re-attributed to NEWMV residual coding drift.
    """
    sign = comp < 0
    mag = -comp if sign else comp
    if mag == 0:
        return 0
    mv_class, offset = _mv_class(mag - 1)
    d = offset >> 3
    decoded_mag = _mv_class_base(mv_class) + ((d << 3) | (3 << 1) | 1) + 1
    return -decoded_mag if sign else decoded_mag


def _check_force_integer_newmv_roundtrip() -> None:
    expected = {
        33: {"mv": (104, -128), "ref": (120, -88)},
        34: {"mv": (40, -128), "ref": (128, -120)},
    }
    for blk, vals in expected.items():
        mvx, mvy = vals["mv"]
        refx, refy = vals["ref"]
        diff_x = mvx - refx
        diff_y = mvy - refy
        got = (
            refx + _force_integer_mv_roundtrip(diff_x),
            refy + _force_integer_mv_roundtrip(diff_y),
        )
        if got != (mvx, mvy):
            fail(
                f"frame-2 blk{blk} force_integer_mv NEWMV round-trip drifted: "
                f"ref=({refx},{refy}) diff=({diff_x},{diff_y}) decoded_mv={got} expected={(mvx, mvy)}"
            )
    print(
        "[PASS] frame-2 Cb blocker NEWMV residuals round-trip exactly under "
        "force_integer_mv for blk33/34, narrowing the remaining +1 to chroma "
        "sampling/filter selection rather than decoded-MV component coding"
    )


def _check_halfpel_ref_signature(dec: Path, recon: Path) -> None:
    """Keep the current Cb blocker narrowed to chroma MC phase/reference math.

    Both failing frame-2 pixels are the row-3/col-1 Cb sample of adjacent
    8x8 luma blocks.  Their MVs are luma-integer but chroma half-sample, so
    they share the same frame-1 Cb reference taps.  This diagnostic proves the
    LAST reference bytes are already decoder/recon equal and records the exact
    small-filter sums that leave RTL recon one LSB below public decoders.
    """
    dec_bytes = dec.read_bytes()
    recon_bytes = recon.read_bytes()
    cb1 = _cb_offset(1)
    row11_x8_15_dec = list(dec_bytes[cb1 + 11 * 32 + 8: cb1 + 11 * 32 + 16])
    row11_x8_15_recon = list(recon_bytes[cb1 + 11 * 32 + 8: cb1 + 11 * 32 + 16])
    expected_ref = [152, 151, 155, 160, 166, 166, 166, 166]
    if row11_x8_15_dec != expected_ref or row11_x8_15_recon != expected_ref:
        fail(
            "frame-1 Cb reference taps drifted for frame-2 blk33/34 halfpel probe: "
            f"decoder={row11_x8_15_dec} recon={row11_x8_15_recon}"
        )

    sample_origins = {
        33: _same_size_chroma_origin(33, 1, 3, 104, -128),
        34: _same_size_chroma_origin(34, 1, 3, 40, -128),
    }
    if sample_origins != {33: (11, 11, 8, 0), 34: (11, 11, 8, 0)}:
        fail(f"unexpected AV1 same-size chroma origin derivation: {sample_origins}")
    wrong_siting_origins = {
        33: _wrong_scaled_siting_origin(33, 1, 3, 104, -128),
        34: _wrong_scaled_siting_origin(34, 1, 3, 40, -128),
    }
    if wrong_siting_origins != {33: (12, 11, 0, 8), 34: (12, 11, 0, 8)}:
        fail(f"unexpected scaled-path chroma-siting contrast: {wrong_siting_origins}")
    libaom_q10_origins = {
        33: _libaom_q10_unscaled_chroma_origin(33, 1, 3, 104, -128),
        34: _libaom_q10_unscaled_chroma_origin(34, 1, 3, 40, -128),
    }
    if libaom_q10_origins != sample_origins:
        fail(
            "current libaom q10 unscaled chroma setup no longer matches the reduced spec derivation: "
            f"spec={sample_origins} libaom={libaom_q10_origins}"
        )

    small_phase8 = (0, 0, -12, 76, 76, -12, 0, 0)
    small_phase9 = (0, 0, -10, 66, 84, -12, 0, 0)
    phase8_pred = _round_filter(expected_ref, small_phase8)
    phase9_pred = _round_filter(expected_ref, small_phase9)
    if (phase8_pred, phase9_pred) != (0xA3, 0xA4):
        fail(f"unexpected Cb halfpel predictor signature phase8={phase8_pred} phase9={phase9_pred}")

    regular8_phase8 = (0, 2, -14, 76, 76, -14, 2, 0)
    regular8_phase9 = (0, 2, -12, 66, 84, -14, 2, 0)
    regular8_pred = _round_filter(expected_ref, regular8_phase8)
    regular9_pred = _round_filter(expected_ref, regular8_phase9)
    if (regular8_pred, regular9_pred) != (0xA3, 0xA4):
        fail(
            "unexpected full regular-filter contrast for Cb halfpel blocker: "
            f"phase8={regular8_pred} phase9={regular9_pred}"
        )
    print(
        "[PASS] frame-2 Cb blocker narrowed: spec and current libaom unscaled "
        "chroma setup keep blk33/34 at base=(11,11) phase=(8,0) and rule out "
        "the scaled-path +8 siting origin base=(12,11) phase=(0,8); frame-1 taps "
        "match public decode; small and full regular phase8 both predict RTL 0xA3 "
        "while neighboring phase9 predicts decoder 0xA4"
    )


def main() -> int:
    if not SIM.exists():
        fail(f"missing simulator {SIM}; run make WIDTH=64 HEIGHT=64 all first")
    require("ffmpeg", "aomdec")
    artifact_root = Path(os.environ.get("AV1_ARTIFACT_ROOT", TB / "artifacts"))
    out = artifact_root / "natural64_ip_fullcoeff_newmv_3frame_probe"
    paths = run_encoder_case(
        out,
        W,
        H,
        frames=3,
        qindex=128,
        all_key=False,
        gop_mode="lowdelay_last",
        key_interval=12,
        pattern="gradient",
        repeat=False,
        dc_only=0,
        timeout=300_000_000,
        extra_plusargs=[
            "+me_newmv_limit=255",
            "+dump_inter_summary=1",
            "+dump_ref_summary=1",
            "+dump_chroma_summary=1",
            "+dump_chroma_detail=1",
            "+dump_chroma_detail_start=33",
            "+dump_chroma_detail_end=34",
        ],
    )
    log = paths["log"]
    check_rtl_ownership(paths, "3-frame 64x64 full-coeff NEWMV widening probe")

    frame1 = re.search(
        r"inter_summary frame=1 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}",
        log,
    )
    frame2 = re.search(
        r"inter_summary frame=2 total_inter=(\d+) nonzero_inter=(\d+) first_inter_blk=(-?\d+) "
        r"mode_counts=\{GLOBALMV:(\d+) NEARESTMV:(\d+) NEARMV:(\d+) NEWMV:(\d+)\}",
        log,
    )
    if not frame1 or not frame2:
        fail("missing frame-1/frame-2 inter summaries")
    if tuple(map(int, frame1.groups())) != (64, 12, 0, 18, 3, 0, 43):
        fail(f"unexpected frame-1 unrestricted signature: {frame1.groups()}")
    if tuple(map(int, frame2.groups())) != (64, 1, 0, 34, 3, 0, 27):
        fail(f"unexpected frame-2 widened signature: {frame2.groups()}")
    for blk in (33, 34):
        if not re.search(rf"inter_summary frame=2 blk={blk} .* mode=NEWMV ", log):
            fail(f"expected frame-2 block {blk} to stay in the NEWMV region")
    _check_expected_inter_stack(log)
    _check_force_integer_newmv_roundtrip()

    expected_chroma_detail = {
        33: "inter=1 mv=(104,-128) cb_has=0 cr_has=1 cb_nz=0 cr_nz=1 "
            "cb_qcoeff=0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 "
            "cr_qcoeff=1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
        34: "inter=1 mv=(40,-128) cb_has=1 cr_has=1 cb_nz=1 cr_nz=1 "
            "cb_qcoeff=1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 "
            "cr_qcoeff=1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
    }
    for blk, expected in expected_chroma_detail.items():
        detail = re.search(rf"\[TB\] chroma_detail frame=2 blk={blk} ([^\n]+)", log)
        if not detail:
            fail(f"missing frame-2 block {blk} chroma detail")
        if detail.group(1) != expected:
            fail(f"unexpected frame-2 block {blk} chroma detail: {detail.group(1)}")

    expected_pixel_detail = {
        33: "cb_pred=144,154,164,162,142,157,170,168,150,160,168,167,157,163,167,166 "
            "cb_recon=144,154,164,162,142,157,170,168,150,160,168,167,157,163,167,166 "
            "cr_pred=141,140,140,140,140,141,141,141,142,141,141,141,144,142,141,141 "
            "cr_recon=145,144,144,144,144,145,145,145,146,145,145,145,148,146,145,145",
        34: "cb_pred=144,154,164,162,142,157,170,168,150,160,168,167,157,163,167,166 "
            "cb_recon=148,158,168,166,146,161,174,172,154,164,172,171,161,167,171,170 "
            "cr_pred=141,140,140,140,140,141,141,141,142,141,141,141,144,142,141,141 "
            "cr_recon=145,144,144,144,144,145,145,145,146,145,145,145,148,146,145,145",
    }
    for blk, expected in expected_pixel_detail.items():
        detail = re.search(rf"\[TB\] chroma_pixel_detail frame=2 blk={blk} ([^\n]+)", log)
        if not detail:
            fail(f"missing frame-2 block {blk} chroma pixel detail")
        if detail.group(1) != expected:
            fail(f"unexpected frame-2 block {blk} chroma pixel detail: {detail.group(1)}")
    print("[PASS] frame-2 Cb blocker blocks have stable chroma coeff/pixel prediction signature")

    rtl_ivf = Path(paths["rtl_ivf"])
    recon = Path(paths["recon"])
    ff_rtl = out / "ff_rtl.yuv"
    aom_rtl = out / "aom_rtl.yuv"
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", rtl_ivf,
         "-f", "rawvideo", "-pix_fmt", "yuv420p", ff_rtl])
    run(["aomdec", "--codec=av1", "--rawvideo", "--i420", "-o", aom_rtl, rtl_ivf])
    _check_public_decoders_agree(ff_rtl, aom_rtl)
    _check_expected_decoder_delta(ff_rtl, recon, "FFmpeg/libdav1d")
    _check_expected_decoder_delta(aom_rtl, recon, "aomdec")
    _check_halfpel_ref_signature(ff_rtl, recon)
    print(
        "[PASS] 3-frame 64x64 full-coeff NEWMV widening probe: bytes match, "
        "public decoders agree on the same narrow frame-2 Cb recon blocker"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
