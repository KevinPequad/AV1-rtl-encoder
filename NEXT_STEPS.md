# Next Steps

## Completed

- Implemented reduced raw-path `NEARESTMV` / `NEWMV` mode signaling plus integer MV payload syntax in `rtl/av1_encoder_top.v` for the current single-reference LAST-ref subset.
- Added reduced neighboring ref-MV stack derivation and per-block integer MV storage so the RTL path now emits real `refmv`, `drl`, `mv_joint`, sign, class, class0, and class-bit syntax.
- Verified reduced natural-motion ownership on the RTL byte path:
  - `output/natural_motion64_x640_y360_rtl2/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `output/natural_motion64_x640_y360_3f/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `output/natural_motion32_x640_y360_3f/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
- Confirmed the first motion-path drift was bad `refmv` / `drl` ICDF porting, not MV payload packing:
  - the writer-side probabilities had to be converted to the real AV1 ICDF entries used by `rtl/av1_entropy.v`
  - correcting those ICDF values restored exactness on the motion clips
- Confirmed that the local `data/tmp_probe_16x16_1f.yuv` byte drift is pre-existing:
  - a temporary revert of the new inter-path patch still produces the same `34` vs `35` byte mismatch
  - do not treat that clip as a regression caused by this slice

## What Remains

- Establish a practical longer multi-frame motion regression guard beyond the current `3`-frame exact cases.
- If the current exhaustive-ME motion runs remain too slow, add or use a narrower reproducible motion guard that still exercises the current reduced LAST-ref motion subset.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- The current `64x64` `5`-frame and `10`-frame natural-motion runs timed out under the present local runtime budget, so there is not yet a practical longer-motion regression guard above `3` frames.
- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate; software vs RTL first differ at byte `14` even with the new inter-path patch temporarily reverted.

## Exact Next Command Or File To Edit

- File to edit: `PROJECT_PLAN.md`
- Next command:
  - `wsl bash -lc 'cd /mnt/c/Users/pie/Desktop/av1stage64/tb && ./obj_dir/Vav1_encoder_top +input=/mnt/c/Users/pie/Desktop/Encdoing\\ Shenanigans/AV1-rtl-encoder/data/natural_motion64_x640_y360_5f.yuv +output=/mnt/c/Users/pie/Desktop/Encdoing\\ Shenanigans/AV1-rtl-encoder/output/natural_motion64_x640_y360_5f/encoded.obu +frames=5 +all_key=0 +qindex=128 +dc_only=0 +dump_inter_summary=1 >/tmp/av1_motion5f_run.log 2>&1 && tail -n 60 /tmp/av1_motion5f_run.log'`
