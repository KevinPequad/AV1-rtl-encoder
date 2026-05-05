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
- Fixed the shared strict-decoder corruption that appeared immediately after the header/subpel move:
  - the first bug was the missing `mv_hp` payload symbol after `mv_fr`
  - the remaining first-inter-frame corruption was the missing `allow_high_precision_mv` frame-header bit after `force_integer_mv=0`
- Verified the syntax-only subpel slice on the current integer-MV motion guards:
  - `output/natural_motion64_x640_y360_2f_subpel2/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
  - `output/natural_motion64_x640_y360_7f_subpel2/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
  - `output/natural_motion64_x640_y360_10f_subpel2/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
- Reconfirmed the current longer-run runtime envelope after the syntax-only subpel step:
  - the full `64x64` natural-motion `10`-frame guard completes at cycle `65670737`
  - keep using `+progress_every=5000000 +timeout=70000000` or higher on this machine
- Landed the first real non-zero fractional-pel translational checkpoint on the reduced single-reference LAST path:
  - `make THREADS=1 BUILD_JOBS=1 me-check` passes and confirms the motion-search refinement is active
  - `make THREADS=1 BUILD_JOBS=1 WIDTH=32 HEIGHT=32 natural32-ip-fractional-syntax-check natural32-ip-newmv-syntax-check natural32-ip-syntax-check` passes with raw OBU/IVF exactness and FFmpeg/libdav1d + `aomdec` decode-to-`recon.yuv` parity on the named 32x32 fixtures
  - `make THREADS=1 BUILD_JOBS=1 natural32-chroma-syntax-check nonzero-chroma16-syntax-check nonzero-chroma-syntax-check` passes on the named chroma fixtures
  - `make THREADS=1 BUILD_JOBS=1 natural64-ip-fractional-syntax-check` passes with raw OBU/IVF exactness and recon parity on the 64x64 natural-motion guard

## What Remains

- Widen beyond the reduced single-reference LAST motion subset into multi-reference/reference-MV-context debugging on broader natural-motion clips.
- Keep the named motion and chroma gates passing while broadening the fixture set.
- Reuse `output/natural_motion64_x640_y360_2f_subpel2/`, `output/natural_motion64_x640_y360_7f_subpel2/`, and `output/natural_motion64_x640_y360_10f_subpel2/` as the current exact syntax-only subpel guards while the broader search work lands.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- No blocker remains on the reduced LAST fractional-pel checkpoint; the named natural32/natural64 motion gates now pass raw/IVF exactness and public-decoder checks.
- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate.
- The full `10`-frame `64x64` natural-motion guard still needs `+timeout=70000000` or higher on this machine.

## Exact Next Command Or File To Edit

- First file to edit for the next motion move: `rtl/av1_me.v`
- Immediate debug target: widen beyond the reduced LAST-only path into multi-reference/reference-MV-context natural-motion debugging.
- Original fractional-pel search command:
  - `rg -n "best_mvx|best_mvy|ref_x|ref_y|cand_x|cand_y|me_mvx|me_mvy|inter_base_x|inter_base_y|>>> 3|<< 3|ref_mv" rtl/av1_me.v rtl/av1_encoder_top.v`
