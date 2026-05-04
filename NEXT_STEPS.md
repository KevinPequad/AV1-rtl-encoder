# Next Steps

## Completed

- Fixed the Chud PC 2 generated `16x16` smoke ownership blocker:
  - frame OBU size back-patching now uses `bs_wr_addr`, the append address used by the RTL byte mux, rather than the drift-prone debug total counter
  - the testbench default now uses static CDF mode (`disable_cdf_update=1`) to match the current RTL entropy subset until adaptive CDF ownership exists
  - the IP bootstrap writer no longer forces a software-only `INTRA_ONLY_FRAME` header for frame 0; it stays on the RTL-owned video keyframe header path
  - `/tmp/av1_chudpc2_smoke.sh` now passes generated non-flat `16x16` all-key and flat `16x16` 2-frame IP checks: raw OBU exact, IVF exact, `ffmpeg` decode matches `recon.yuv`, and `aomdec` decode matches `recon.yuv`

- Landed the first fractional-pel syntax-only inter slice on the reduced LAST-only path:
  - `tb/av1_bitstream_writer.h`, `rtl/av1_encoder_top.v`, and `rtl/av1_bitstream.v` now emit `force_integer_mv=0`, `allow_high_precision_mv=1`, plus the real `mv_fr` and `mv_hp` symbols on reduced `NEWMV` components
  - `tb/test_rtl_bitstream.cpp` and `make bitstream-check WIDTH=16 HEIGHT=16` now lock that reduced inter header / payload syntax order
  - `output/natural_motion64_x640_y360_2f_subpel2/`, `output/natural_motion64_x640_y360_7f_subpel2/`, and `output/natural_motion64_x640_y360_10f_subpel2/` stay byte-exact and strict-`aomdec` clean

- Landed and post-merge validated the controlled natural32 inter widening at commit `91e403a030b951be45b4954e5dfe433e8418272c`:
  - `natural32-ip-syntax-check` proves the two-frame 32x32 zero-MV natural-ish IP residual gate
  - `natural32-ip-newmv-syntax-check` proves a shifted 32x32 non-zero-MV reduced LAST gate
  - `natural32-ip-fractional-syntax-check` proves a half-pel 32x32 gate with exactly two small `NEWMV` blocks and at least one fractional q3 MV
  - all three gates compare RTL raw OBU bytes against the oracle and verify FFmpeg/libdav1d plus `aomdec` decode-to-`recon.yuv` parity
  - `natural32-chroma-syntax-check`, `nonzero-chroma-syntax-check`, and `nonzero-chroma16-syntax-check` also remained green in the same post-merge validation run

## What Remains

- Widen beyond the controlled 32x32 natural-ish fractional `NEWMV` proof into less constrained, larger, and longer natural-motion clips without losing RTL byte ownership.
- Keep the natural32 inter/chroma public-decoder gate family green while scaling the motion path.
- Continue replacing debug/software writer ownership with RTL-owned final syntax; the current gates are ownership checkpoints, not final full-AV1 completion.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- No current Chud PC 2 generated `16x16` smoke ownership blocker; the all-key and 2-frame IP generated smoke cases now pass raw/IVF exactness and public-decoder checks.
- No current blocker in the controlled natural32 inter/chroma gate family after post-merge validation.
- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate.
- The full `10`-frame `64x64` natural-motion guard still needs `+timeout=70000000` or higher on this machine.

## Exact Next Command Or File To Edit

- First regression command before the next motion widening:

```bash
cd tb
make THREADS=16 BUILD_JOBS=16 natural32-ip-fractional-syntax-check natural32-ip-newmv-syntax-check natural32-ip-syntax-check natural32-chroma-syntax-check nonzero-chroma-syntax-check nonzero-chroma16-syntax-check
```

- First implementation/debug area for the next ownership move: the reduced LAST-only fractional/inter path across `rtl/av1_me.v`, `rtl/av1_encoder_top.v`, and the corresponding natural-motion testbench fixtures.
- Immediate debug target: less constrained / larger natural-motion fractional `NEWMV` coverage beyond the current `32x32`, `+me_newmv_limit=2` proof.
