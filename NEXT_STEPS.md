# Next Steps

## Completed

- Fixed the ME bottom-right zero-skip bug in `rtl/av1_me.v`:
  - the raster helper no longer skips the already-tested zero MV when zero is also the final legal search candidate
  - that bug previously pushed the scan beyond the valid search window and left `TS_WAIT_ME` spinning on longer clips
- Extended exact natural-motion verification beyond the old `3`-frame wall:
  - `output/natural_motion32_x640_y360_5f_fix1/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `output/natural_motion64_x640_y360_5f_fix1/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
  - `output/natural_motion64_x640_y360_6f_fix1/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, decoded RTL IVF matches `recon.yuv`
- Narrowed the first remaining longer-sequence ownership drift:
  - `output/natural_motion64_x640_y360_7f_probe/` first differs at byte `449`
  - that is the start of frame `0006` tile data, so the first six frames are now exact on the RTL-owned path
- Confirmed that the local `data/tmp_probe_16x16_1f.yuv` byte drift is pre-existing:
  - a temporary revert of the new inter-path patch still produces the same `34` vs `35` byte mismatch
  - do not treat that clip as a regression caused by this slice

## What Remains

- Debug the first `64x64` natural-motion ownership drift on `output/natural_motion64_x640_y360_7f_probe/`.
- Identify the first frame-`0006` entropy / syntax mismatch with the existing writer and RTL trace hooks, then restore exactness on that `7`-frame repro.
- Rerun the `10`-frame `64x64` natural-motion guard after the `7`-frame repro is exact.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- The old longer-motion runtime wall is removed, but the first remaining longer-sequence ownership drift now starts on `output/natural_motion64_x640_y360_7f_probe/` at byte `449`.
- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate; software vs RTL first differ at byte `14` even with the new inter-path patch temporarily reverted.

## Exact Next Command Or File To Edit

- File to edit: `rtl/av1_encoder_top.v`
- Next command:
  - `wsl bash -lc 'cd /mnt/c/Users/pie/Desktop/av1stage64/tb && ./obj_dir/Vav1_encoder_top +input=/mnt/c/Users/pie/Desktop/Encdoing\\ Shenanigans/AV1-rtl-encoder/data/natural_motion64_x640_y360_10f.yuv +output=/mnt/c/Users/pie/Desktop/Encdoing\\ Shenanigans/AV1-rtl-encoder/output/natural_motion64_x640_y360_7f_trace/encoded.obu +frames=7 +all_key=0 +qindex=128 +dc_only=0 +trace_entropy=1 +trace_writer_entropy=1 >/tmp/av1_motion64_7f_trace.log 2>&1'`
