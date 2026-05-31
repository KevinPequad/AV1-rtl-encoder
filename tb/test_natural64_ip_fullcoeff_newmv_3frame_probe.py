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


def _check_ffmpeg_skip_loop_filter_not_cause(paths: dict[str, str | Path], ff_dec: Path) -> None:
    """Prove the narrow Cb +1 is not hidden in AV1 post-recon filtering.

    The bitstream already signals loop-filter level zero, CDEF disabled, and
    restoration disabled, but this keeps that assumption executable against a
    public decoder: forcing FFmpeg/libdav1d to skip loop filters must produce
    byte-identical output to the normal public decode and preserve the same two
    Cb deltas versus RTL recon.
    """
    out = ff_dec.with_name("ff_rtl_skip_loop_filter_all.yuv")
    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-skip_loop_filter", "all", "-i", Path(paths["rtl_ivf"]),
        "-f", "rawvideo", "-pix_fmt", "yuv420p", out,
    ])
    got = _mismatches(out, ff_dec)
    if got:
        fail(f"FFmpeg skip_loop_filter=all decode drifted from normal decode: {got[:8]} count={len(got)}")
    _check_expected_decoder_delta(out, Path(paths["recon"]), "FFmpeg/libdav1d skip_loop_filter=all")
    print(
        "[PASS] FFmpeg skip_loop_filter=all is byte-identical to normal decode; "
        "the frame-2 Cb +1 remains, ruling out post-recon loop filtering/CDEF/restoration"
    )


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


def _cb_block(data: bytes, frame: int, blk: int) -> list[int]:
    cb0 = _cb_offset(frame)
    bx = (blk % (W // 8)) * 4
    by = (blk // (W // 8)) * 4
    vals: list[int] = []
    for y in range(4):
        vals.extend(data[cb0 + (by + y) * (W // 2) + bx: cb0 + (by + y) * (W // 2) + bx + 4])
    return vals


def _check_public_cb_block_signature(ff_dec: Path, aom_dec: Path, recon: Path) -> None:
    """Pin the public-decoder Cb block vectors around the 3-frame blocker.

    The scalar mismatch check proves there are only two bad bytes, but keeping
    the full 4x4 public-decoder blocks executable is a stronger diagnostic for
    the next step: the decoders agree with RTL on every neighboring Cb sample,
    and blk34 carries the same +4 DC residual as RTL.  The remaining +1 is
    therefore the local inter predictor sample before residual addition, not a
    wider Cb residual scan, transform, or frame-buffer corruption.
    """
    ff = ff_dec.read_bytes()
    aom = aom_dec.read_bytes()
    rtl = recon.read_bytes()
    expected = {
        33: {
            "public": [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 164, 167, 166],
            "rtl":    [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 163, 167, 166],
            "delta": [(1, 3, 0xA4, 0xA3)],
        },
        34: {
            "public": [148, 158, 168, 166, 146, 161, 174, 172, 154, 164, 172, 171, 161, 168, 171, 170],
            "rtl":    [148, 158, 168, 166, 146, 161, 174, 172, 154, 164, 172, 171, 161, 167, 171, 170],
            "delta": [(1, 3, 0xA8, 0xA7)],
        },
    }
    for blk, exp in expected.items():
        ff_blk = _cb_block(ff, 2, blk)
        aom_blk = _cb_block(aom, 2, blk)
        rtl_blk = _cb_block(rtl, 2, blk)
        if ff_blk != exp["public"] or aom_blk != exp["public"] or rtl_blk != exp["rtl"]:
            fail(
                f"frame-2 blk{blk} Cb block signature drifted: "
                f"ff={ff_blk} aom={aom_blk} rtl={rtl_blk}"
            )
        delta = []
        for idx, (dec_v, rtl_v) in enumerate(zip(ff_blk, rtl_blk)):
            if dec_v != rtl_v:
                delta.append((idx % 4, idx // 4, dec_v, rtl_v))
        if delta != exp["delta"]:
            fail(f"frame-2 blk{blk} Cb local delta drifted: {delta}")
    print(
        "[PASS] public decoders agree with RTL on the full blk33/34 Cb 4x4 "
        "neighborhoods except local sample (1,3); blk34 preserves the same +4 "
        "DC residual, narrowing the +1 to inter predictor input/phase"
    )


def _cb_offset(frame: int) -> int:
    return frame * (W * H * 3 // 2) + (W * H)


def _round_filter_with_offset(samples: list[int], coeffs: tuple[int, ...], offset: int) -> int:
    return max(0, min(255, (sum(c * s for c, s in zip(coeffs, samples)) + offset) >> 7))


def _round_filter(samples: list[int], coeffs: tuple[int, ...]) -> int:
    return _round_filter_with_offset(samples, coeffs, 64)


I_COSPI_16 = 3784
I_COSPI_32 = 2896
I_COSPI_48 = 1567


def _half_btf(w0: int, a: int, w1: int, b: int, cos_bit: int = 12) -> int:
    return (w0 * a + w1 * b + (1 << (cos_bit - 1))) >> cos_bit


def _round_shift(v: int, shift: int) -> int:
    return (v + (1 << (shift - 1))) >> shift


def _idct4(vec: list[int]) -> list[int]:
    # Mirrors the 4x4 inverse transform path used by tb/test_chroma_residual.cpp.
    bf = [vec[0], vec[2], vec[1], vec[3]]
    st0 = _half_btf(I_COSPI_32, bf[0], I_COSPI_32, bf[1])
    st1 = _half_btf(I_COSPI_32, bf[0], -I_COSPI_32, bf[1])
    st2 = _half_btf(I_COSPI_48, bf[2], -I_COSPI_16, bf[3])
    st3 = _half_btf(I_COSPI_16, bf[2], I_COSPI_48, bf[3])
    return [st0 + st3, st1 + st2, st1 - st2, st0 - st3]


def _chroma_inv4x4_residual(qcoeff: list[int]) -> list[int]:
    # qindex=128 reduced chroma dequants, matching av1_chroma_residual.
    dq = [v * (140 if i == 0 else 176) for i, v in enumerate(qcoeff)]
    inv_tmp = [0] * 16
    out = [0] * 16
    for y in range(4):
        row = _idct4([dq[y * 4 + x] for x in range(4)])
        for x in range(4):
            inv_tmp[y * 4 + x] = row[x] >> 1
    for x in range(4):
        col = _idct4([inv_tmp[y * 4 + x] for y in range(4)])
        for y in range(4):
            out[y * 4 + x] = _round_shift(col[y], 3)
    return out


def _clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, v))


def _filtered_cb_block_from_frame1(
    data: bytes,
    blk: int,
    mvx_q3: int,
    mvy_q3: int,
    coeffs: tuple[int, ...],
    *,
    rounding_offset: int = 64,
) -> list[int]:
    """Return the horizontal-only Cb predictor for a frame-2 block from frame-1 taps."""
    cb1 = _cb_offset(1)
    blk_cols = W // 8
    block_x = (blk % blk_cols) * 4
    block_y = (blk // blk_cols) * 4
    base_x = block_x + (mvx_q3 >> 4)
    base_y = block_y + (mvy_q3 >> 4)
    out: list[int] = []
    for y in range(4):
        for x in range(4):
            taps = []
            for tap in range(8):
                sx = _clamp(base_x + x + tap - 3, 0, W // 2 - 1)
                sy = _clamp(base_y + y, 0, H // 2 - 1)
                taps.append(data[cb1 + sy * (W // 2) + sx])
            out.append(_round_filter_with_offset(taps, coeffs, rounding_offset))
    return out


SMALL_REGULAR_FILTERS: tuple[tuple[int, ...], ...] = (
    (0, 0, 0, 128, 0, 0, 0, 0),
    (0, 0, -4, 126, 8, -2, 0, 0),
    (0, 0, -8, 122, 18, -4, 0, 0),
    (0, 0, -10, 116, 28, -6, 0, 0),
    (0, 0, -12, 110, 38, -8, 0, 0),
    (0, 0, -12, 102, 48, -10, 0, 0),
    (0, 0, -14, 94, 58, -10, 0, 0),
    (0, 0, -12, 84, 66, -10, 0, 0),
    (0, 0, -12, 76, 76, -12, 0, 0),
    (0, 0, -10, 66, 84, -12, 0, 0),
    (0, 0, -10, 58, 94, -14, 0, 0),
    (0, 0, -10, 48, 102, -12, 0, 0),
    (0, 0, -8, 38, 110, -12, 0, 0),
    (0, 0, -6, 28, 116, -10, 0, 0),
    (0, 0, -4, 18, 122, -8, 0, 0),
    (0, 0, -2, 8, 126, -4, 0, 0),
)


SMOOTH_4TAP_FILTERS: tuple[tuple[int, ...], ...] = (
    (0, 0, 0, 128, 0, 0, 0, 0),
    (0, 0, 30, 62, 34, 2, 0, 0),
    (0, 0, 26, 62, 36, 4, 0, 0),
    (0, 0, 22, 62, 40, 4, 0, 0),
    (0, 0, 20, 60, 42, 6, 0, 0),
    (0, 0, 18, 58, 44, 8, 0, 0),
    (0, 0, 16, 56, 46, 10, 0, 0),
    (0, 0, 14, 54, 48, 12, 0, 0),
    (0, 0, 12, 52, 52, 12, 0, 0),
    (0, 0, 12, 48, 54, 14, 0, 0),
    (0, 0, 10, 46, 56, 16, 0, 0),
    (0, 0, 8, 44, 58, 18, 0, 0),
    (0, 0, 6, 42, 60, 20, 0, 0),
    (0, 0, 4, 40, 62, 22, 0, 0),
    (0, 0, 4, 36, 62, 26, 0, 0),
    (0, 0, 2, 34, 62, 30, 0, 0),
)

BILINEAR_FILTERS: tuple[tuple[int, ...], ...] = tuple(
    (0, 0, 0, 128 - 8 * phase, 8 * phase, 0, 0, 0) for phase in range(16)
)


def _cb_ref_sample(data: bytes, x: int, y: int) -> int:
    cb1 = _cb_offset(1)
    sx = _clamp(x, 0, W // 2 - 1)
    sy = _clamp(y, 0, H // 2 - 1)
    return data[cb1 + sy * (W // 2) + sx]


def _predict_cb_candidate(data: bytes, base_x: int, base_y: int, phase_x: int, phase_y: int) -> list[int]:
    """Predict a 4x4 Cb block from frame 1 for one uniform base/phase candidate."""
    out: list[int] = []
    if phase_x == 0 and phase_y == 0:
        for y in range(4):
            for x in range(4):
                out.append(_cb_ref_sample(data, base_x + x, base_y + y))
        return out
    if phase_y == 0:
        coeffs_x = SMALL_REGULAR_FILTERS[phase_x]
        for y in range(4):
            for x in range(4):
                taps = [_cb_ref_sample(data, base_x + x + tap - 3, base_y + y) for tap in range(8)]
                out.append(_round_filter(taps, coeffs_x))
        return out
    if phase_x == 0:
        coeffs_y = SMALL_REGULAR_FILTERS[phase_y]
        for y in range(4):
            for x in range(4):
                taps = [_cb_ref_sample(data, base_x + x, base_y + y + tap - 3) for tap in range(8)]
                out.append(_round_filter(taps, coeffs_y))
        return out

    coeffs_x = SMALL_REGULAR_FILTERS[phase_x]
    coeffs_y = SMALL_REGULAR_FILTERS[phase_y]
    for y in range(4):
        for x in range(4):
            vertical_acc = 0
            for tap_y in range(8):
                taps = [
                    _cb_ref_sample(data, base_x + x + tap_x - 3, base_y + y + tap_y - 3)
                    for tap_x in range(8)
                ]
                horizontal = (sum(coeff * sample for coeff, sample in zip(coeffs_x, taps)) + 4) >> 3
                vertical_acc += coeffs_y[tap_y] * horizontal
            out.append(max(0, min(255, (vertical_acc + 1024) >> 11)))
    return out


def _check_no_single_coeff_residual_explanation() -> None:
    """Reject a one-symbol Cb residual explanation for the public +1 sample.

    The blk33 public-decoder Cb block differs from RTL prediction by only sample
    index 13.  If this were a simple Cb coefficient coding drift rather than an
    inter predictor/sample-selection issue, a small single qcoeff should be able
    to synthesize the same sparse +1 residual through the reduced q128 4x4 inverse
    transform.  The executable search below proves it cannot: every nonzero
    single coefficient spreads to all 16 pixels, and the closest single-symbol
    explanation is only DC +1, which adds +4 everywhere.
    """
    target = [0] * 16
    target[13] = 1
    matches: list[tuple[int, int]] = []
    best: tuple[int, int, int, int, list[int]] | None = None
    min_nz = 16
    for idx in range(16):
        for val in range(-8, 9):
            if val == 0:
                continue
            qcoeff = [0] * 16
            qcoeff[idx] = val
            residual = _chroma_inv4x4_residual(qcoeff)
            nz = sum(1 for v in residual if v != 0)
            min_nz = min(min_nz, nz)
            if residual == target:
                matches.append((idx, val))
            sad = sum(abs(a - b) for a, b in zip(residual, target))
            candidate = (sad, nz, idx, val, residual)
            if best is None or candidate < best:
                best = candidate
    if matches:
        fail(f"unexpected single-qcoeff Cb residual explanation for blk33/34 +1 sample: {matches}")
    expected_best = (63, 16, 0, 1, [4] * 16)
    if best != expected_best or min_nz != 16:
        fail(f"single-qcoeff Cb residual search drifted: best={best} min_nz={min_nz}")
    print(
        "[PASS] no single q128 4x4 Cb coefficient can explain the public +1 sample: "
        "all one-symbol residuals affect all 16 pixels; closest is DC+1 => +4 everywhere"
    )


def _check_no_uniform_subpel_candidate(dec: Path) -> None:
    """Reject the next broad workaround: a single nearby base/phase for the whole block.

    The public Cb predictor vector is current RTL phase-8 output plus one local
    LSB at sample 13.  If a nearby legal AV1 chroma origin/phase could produce
    the full public 4x4 vector, the fix would likely be decoded-MV phase/origin
    selection.  This exhaustive local scan keeps that hypothesis executable:
    all q4 phases and a generous +/-3 base window around the current origin are
    tried, and none reproduces the full public block.  The nearest uniform
    candidate remains the current RTL base=(10,8), phase=(8,0), one LSB low at
    only sample 13.
    """
    data = dec.read_bytes()
    public_predictor = [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 164, 167, 166]
    rtl_predictor = [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 163, 167, 166]
    matches: list[tuple[int, int, int, int]] = []
    best: tuple[int, int, int, int, int, list[int]] | None = None
    for base_x in range(7, 14):
        for base_y in range(5, 12):
            for phase_x in range(16):
                for phase_y in range(16):
                    block = _predict_cb_candidate(data, base_x, base_y, phase_x, phase_y)
                    sad = sum(abs(got - exp) for got, exp in zip(block, public_predictor))
                    if block == public_predictor:
                        matches.append((base_x, base_y, phase_x, phase_y))
                    if best is None or (sad, base_x, base_y, phase_x, phase_y) < best[:5]:
                        best = (sad, base_x, base_y, phase_x, phase_y, block)
    if matches:
        fail(f'unexpected uniform Cb base/phase candidate matches public predictor: {matches[:8]}')
    expected_best = (1, 10, 8, 8, 0, rtl_predictor)
    if best != expected_best:
        fail(f'uniform Cb base/phase scan drifted: best={best} expected={expected_best}')
    print(
        '[PASS] no nearby uniform Cb base/phase candidate reproduces the public '
        'blk33/34 predictor: scanned base_x=7..13 base_y=5..11 and all q4 phases; '
        'best remains current base=(10,8) phase=(8,0), one LSB low only at sample 13'
    )


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
    phase8_sum = sum(coeff * sample for coeff, sample in zip(small_phase8, expected_ref))
    phase9_sum = sum(coeff * sample for coeff, sample in zip(small_phase9, expected_ref))
    phase8_pred = _round_filter(expected_ref, small_phase8)
    phase9_pred = _round_filter(expected_ref, small_phase9)
    if (phase8_sum, phase9_sum, phase8_pred, phase9_pred) != (20924, 20962, 0xA3, 0xA4):
        fail(
            "unexpected Cb halfpel predictor threshold signature: "
            f"phase8_sum={phase8_sum} phase9_sum={phase9_sum} "
            f"phase8={phase8_pred} phase9={phase9_pred}"
        )
    phase8_round_up_threshold = (0xA4 << 7) - 64
    if phase8_round_up_threshold - phase8_sum != 4:
        fail(
            "frame-2 Cb phase8 sample is no longer exactly four filter-sum units "
            f"below the public 0xA4 round-up threshold: sum={phase8_sum} "
            f"threshold={phase8_round_up_threshold}"
        )

    regular8_phase8 = (0, 2, -14, 76, 76, -14, 2, 0)
    regular8_phase9 = (0, 2, -12, 66, 84, -14, 2, 0)
    regular8_pred = _round_filter(expected_ref, regular8_phase8)
    regular9_pred = _round_filter(expected_ref, regular8_phase9)
    if (regular8_pred, regular9_pred) != (0xA3, 0xA4):
        fail(
            "unexpected full regular-filter contrast for Cb halfpel blocker: "
            f"phase8={regular8_pred} phase9={regular9_pred}"
        )

    expected_phase8_block = [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 163, 167, 166]
    expected_phase9_block = [144, 155, 163, 162, 142, 158, 170, 168, 150, 161, 168, 167, 158, 164, 166, 166]
    expected_public_predictor = [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 164, 167, 166]
    expected_round68_block = expected_public_predictor
    phase9_deltas = [(1, 155, 154), (2, 163, 164), (5, 158, 157), (9, 161, 160), (12, 158, 157), (14, 166, 167)]
    round68_deltas = [(13, 163, 164)]
    for blk, mv in {33: (104, -128), 34: (40, -128)}.items():
        phase8_block = _filtered_cb_block_from_frame1(dec_bytes, blk, mv[0], mv[1], small_phase8)
        phase9_block = _filtered_cb_block_from_frame1(dec_bytes, blk, mv[0], mv[1], small_phase9)
        if phase8_block != expected_phase8_block:
            fail(f"frame-2 blk{blk} Cb phase8 full-block predictor drifted: {phase8_block}")
        if phase9_block != expected_phase9_block:
            fail(f"frame-2 blk{blk} Cb phase9 full-block predictor drifted: {phase9_block}")
        round68_block = _filtered_cb_block_from_frame1(
            dec_bytes, blk, mv[0], mv[1], small_phase8, rounding_offset=68
        )
        if round68_block != expected_round68_block:
            fail(f"frame-2 blk{blk} Cb round+68 contrast drifted: {round68_block}")
        got_phase9_deltas = [
            (idx, phase9_v, public_v)
            for idx, (phase9_v, public_v) in enumerate(zip(phase9_block, expected_public_predictor))
            if phase9_v != public_v
        ]
        if got_phase9_deltas != phase9_deltas:
            fail(f"frame-2 blk{blk} blanket phase9 contrast drifted: {got_phase9_deltas}")
        got_round68_deltas = [
            (idx, phase8_v, round68_v)
            for idx, (phase8_v, round68_v) in enumerate(zip(phase8_block, round68_block))
            if phase8_v != round68_v
        ]
        if got_round68_deltas != round68_deltas:
            fail(f"frame-2 blk{blk} round+68 contrast drifted: {got_round68_deltas}")

    print(
        "[PASS] frame-2 Cb blocker narrowed: spec and current libaom unscaled "
        "chroma setup keep blk33/34 at base=(11,11) phase=(8,0) and rule out "
        "the scaled-path +8 siting origin base=(12,11) phase=(0,8); frame-1 taps "
        "match public decode; small and full regular phase8 both predict RTL 0xA3 "
        "while neighboring phase9 predicts decoder 0xA4, but a blanket phase9 bump "
        "would introduce six other Cb predictor deltas per block; a local round+68 "
        "contrast only flips the observed sample and remains a rejected non-spec workaround"
    )


def _check_single_tap_equivalent_delta(dec: Path) -> None:
    """Pin the exact reference-tap perturbations that would explain the +1.

    The public-decoder Cb predictor for blk33/34 differs from the current phase-8
    RTL predictor by only local sample index 13.  Keeping the reference-tap
    sensitivity executable narrows the next source trace: if the public decoder
    is still using the current base=(10,8), phase=(8,0), the observed +1 is
    equivalent to exactly one of two single-byte tap interpretations on frame-1
    Cb row 11: sample x=10 one lower, or sample x=12 one higher.  Broader
    reference corruption is already rejected by frame-1 decoder/recon equality.
    """
    data = dec.read_bytes()
    public_predictor = [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 164, 167, 166]
    matches: list[tuple[int, int, int, int]] = []
    cb1 = _cb_offset(1)
    row_y = 11
    for x in range(7, 18):
        off = cb1 + row_y * (W // 2) + x
        orig = data[off]
        for delta in range(-8, 9):
            if delta == 0:
                continue
            patched = bytearray(data)
            patched[off] = _clamp(orig + delta, 0, 255)
            block = _filtered_cb_block_from_frame1(bytes(patched), 33, 104, -128, SMALL_REGULAR_FILTERS[8])
            if block == public_predictor:
                matches.append((x, row_y, orig, delta))
    expected = [(10, 11, 155, -1), (12, 11, 166, 1)]
    if matches != expected:
        fail(f"frame-2 Cb single-tap equivalent scan drifted: {matches} expected={expected}")
    print(
        "[PASS] frame-2 Cb +1 is equivalent under current phase8 math to exactly "
        "two single frame-1 Cb tap interpretations: row11 x10=154 or x12=167; "
        "the next source trace should explain which public-decoder sample input differs"
    )



def _check_no_filter_family_explanation(dec: Path) -> None:
    """Reject switchable-filter family drift as the blk33/34 Cb +1 explanation.

    For chroma width <= 4, libaom maps regular and sharp to the 4-tap regular
    table; smooth has a separate 4-tap table.  This check also includes the
    bilinear contrast so the remaining one-LSB public-decoder delta is not
    attributed to a broad interpolation-filter-family mismatch.
    """
    data = dec.read_bytes()
    public_predictor = [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 164, 167, 166]
    expected_phase8 = {
        "regular4_or_sharp4": [144, 154, 164, 162, 142, 157, 170, 168, 150, 160, 168, 167, 157, 163, 167, 166],
        "smooth4": [146, 154, 161, 162, 146, 156, 166, 168, 152, 160, 166, 167, 158, 163, 165, 166],
        "bilinear": [145, 154, 162, 162, 144, 157, 168, 168, 151, 160, 167, 167, 158, 163, 166, 166],
    }
    filter_tables = (
        ("regular4_or_sharp4", SMALL_REGULAR_FILTERS),
        ("smooth4", SMOOTH_4TAP_FILTERS),
        ("bilinear", BILINEAR_FILTERS),
    )
    for name, table in filter_tables:
        block = _filtered_cb_block_from_frame1(data, 33, 104, -128, table[8])
        if block != expected_phase8[name]:
            fail(f"frame-2 blk33 {name} phase8 predictor contrast drifted: {block}")
        block34 = _filtered_cb_block_from_frame1(data, 34, 40, -128, table[8])
        if block34 != expected_phase8[name]:
            fail(f"frame-2 blk34 {name} phase8 predictor contrast drifted: {block34}")

    matches: list[tuple[str, int]] = []
    best: tuple[int, int, str, int, list[int]] | None = None
    for order, (name, table) in enumerate(filter_tables):
        for phase in range(16):
            block = _filtered_cb_block_from_frame1(data, 33, 104, -128, table[phase])
            if block == public_predictor:
                matches.append((name, phase))
            sad = sum(abs(got - exp) for got, exp in zip(block, public_predictor))
            candidate = (sad, order, name, phase, block)
            if best is None or candidate < best:
                best = candidate
    expected_best = (1, 0, "regular4_or_sharp4", 8, expected_phase8["regular4_or_sharp4"])
    if matches or best != expected_best:
        fail(f"unexpected filter-family explanation for public Cb predictor: matches={matches} best={best}")
    print(
        "[PASS] no AV1 width<=4 interpolation-filter family explains the public "
        "blk33/34 Cb +1: regular/sharp phase8 remains the nearest one-LSB-low "
        "candidate, while smooth4/bilinear drift multiple neighboring samples"
    )


def _check_sensitive_tap_producers(log: str, dec: Path, recon: Path) -> None:
    """Map the two sensitive frame-1 Cb taps back to their producer blocks.

    The single-tap equivalence scan leaves only row11/x10 and row11/x12 as
    possible local explanations for the public +1.  This check pins those two
    samples to the frame-1 NEWMV producer blocks and proves their public-decoder
    output still matches RTL recon byte-for-byte, so the next investigation can
    trace public-decoder internal reference sampling rather than broad frame-1
    reconstruction drift.
    """
    expected_lines = {
        18: "[TB] inter_summary frame=1 blk=18 mv=(88,24) ref=(0,32) near=(-40,88) "
            "mode=NEWMV mode_ctx=84 ctx(new=4 zero=0 ref=5) dc=0 nz=1 cand_count=6 "
            "cand0=(0,32,w=644) cand1=(-40,88,w=644) cand2=(32,24,w=644) "
            "cand3=(-64,96,w=4) cand4=(-16,40,w=4) cand5=(96,-48,w=4)",
        19: "[TB] inter_summary frame=1 blk=19 mv=(128,56) ref=(32,24) near=(88,24) "
            "mode=NEWMV mode_ctx=84 ctx(new=4 zero=0 ref=5) dc=0 nz=0 cand_count=6 "
            "cand0=(32,24,w=644) cand1=(88,24,w=644) cand2=(0,32,w=4) "
            "cand3=(-64,72,w=4) cand4=(-40,88,w=4) cand5=(96,-48,w=4)",
    }
    for blk, expected in expected_lines.items():
        line = re.search(rf"\[TB\] inter_summary frame=1 blk={blk} [^\n]+", log)
        if not line:
            fail(f"missing frame-1 sensitive Cb producer block {blk} inter summary")
        if line.group(0) != expected:
            fail(f"frame-1 sensitive Cb producer block {blk} summary drifted: {line.group(0)}")

    dec_bytes = dec.read_bytes()
    recon_bytes = recon.read_bytes()
    expected_blocks = {
        18: [142, 142, 144, 146, 141, 141, 143, 145, 147, 146, 149, 153, 152, 151, 155, 160],
        19: [162, 162, 162, 162, 168, 168, 168, 168, 167, 167, 167, 167, 166, 166, 166, 166],
    }
    for blk, expected in expected_blocks.items():
        dec_block = _cb_block(dec_bytes, 1, blk)
        recon_block = _cb_block(recon_bytes, 1, blk)
        if dec_block != expected or recon_block != expected:
            fail(f"frame-1 sensitive Cb producer block {blk} drifted: decoder={dec_block} recon={recon_block}")

    cb1 = _cb_offset(1)
    sensitive = {
        (10, 11): (18, 14, 155),
        (12, 11): (19, 12, 166),
    }
    for (x, y), (blk, idx, expected_value) in sensitive.items():
        off = cb1 + y * (W // 2) + x
        if dec_bytes[off] != expected_value or recon_bytes[off] != expected_value:
            fail(
                f"sensitive frame-1 Cb tap x{x}/y{y} drifted: "
                f"decoder={dec_bytes[off]} recon={recon_bytes[off]} expected={expected_value}"
            )
        if expected_blocks[blk][idx] != expected_value:
            fail(f"sensitive tap-to-block mapping drifted for x{x}/y{y}: blk={blk} idx={idx}")
    print(
        "[PASS] sensitive frame-1 Cb taps are pinned to decoder/recon-equal NEWMV "
        "producer blocks: row11 x10=155 from blk18 sample14 and row11 x12=166 "
        "from blk19 sample12; next trace should inspect public decoder internal "
        "reference sampling at these two taps"
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
    _check_ffmpeg_skip_loop_filter_not_cause(paths, ff_rtl)
    _check_expected_decoder_delta(ff_rtl, recon, "FFmpeg/libdav1d")
    _check_expected_decoder_delta(aom_rtl, recon, "aomdec")
    _check_public_cb_block_signature(ff_rtl, aom_rtl, recon)
    _check_no_single_coeff_residual_explanation()
    _check_halfpel_ref_signature(ff_rtl, recon)
    _check_single_tap_equivalent_delta(ff_rtl)
    _check_no_filter_family_explanation(ff_rtl)
    _check_sensitive_tap_producers(log, ff_rtl, recon)
    _check_no_uniform_subpel_candidate(ff_rtl)
    print(
        "[PASS] 3-frame 64x64 full-coeff NEWMV widening probe: bytes match, "
        "public decoders agree on the same narrow frame-2 Cb recon blocker"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
