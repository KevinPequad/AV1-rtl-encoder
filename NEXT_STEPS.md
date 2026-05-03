# Next Steps

## Completed

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

## What Remains

- Enable the first real non-zero fractional-pel translational checkpoint on the reduced LAST-only path.
- Bring fractional predictor / ME behavior onto the smallest natural-motion guard without regressing the new syntax-only subpel checkpoints.
- Reuse `output/natural_motion64_x640_y360_2f_subpel2/`, `output/natural_motion64_x640_y360_7f_subpel2/`, and `output/natural_motion64_x640_y360_10f_subpel2/` after the first non-zero subpel datapath step lands.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- Chud PC 2 top-level smoke blocker: after the Verilator-5.020 compile fixes, generated non-flat `16x16` all-key and flat `16x16` 2-frame IP RTL raw outputs are not byte-exact with the software-owned path; the non-flat all-key / 2-frame IP RTL IVF can be rejected by public decoders because of OBU length/payload drift.
- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate.
- The full `10`-frame `64x64` natural-motion guard still needs `+timeout=70000000` or higher on this machine.

## Exact Next Command Or File To Edit

- First blocker file to inspect: `rtl/av1_encoder_top.v`
- Immediate debug target: RTL raw byte capture / OBU payload length drift on the Chud PC 2 generated `16x16` smoke cases, before widening fractional-pel datapath work.
- After that drift is fixed, return to file: `rtl/av1_me.v`
- Original fractional-pel search command:
  - `rg -n "best_mvx|best_mvy|ref_x|ref_y|cand_x|cand_y|me_mvx|me_mvy|inter_base_x|inter_base_y|>>> 3|<< 3" rtl/av1_me.v rtl/av1_encoder_top.v`
