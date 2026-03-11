# Next Steps

## Completed

- Fixed the reduced LAST-ref MV stack reach in `tb/av1_bitstream_writer.h` and `rtl/av1_encoder_top.v`:
  - the reduced row/column scans now stop where AOM's `MVREF_ROW_COLS == 3` scan stops
  - that removed the extra far-neighbor `+4` weight that was drifting later NEWMV candidates on the longer natural-motion guard
- Restored exact longer-motion natural-motion ownership:
  - `output/natural_motion64_x640_y360_7f_fixmvref64/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
  - `output/natural_motion64_x640_y360_10f_progress70m/`: `encoded.obu` matches `encoded_rtl_raw.obu`, `encoded.ivf` matches `encoded_rtl.ivf`, and strict `aomdec` output matches `recon.yuv`
- Proved the earlier `10`-frame timeout was runtime envelope, not a deadlock:
  - the full `64x64` natural-motion `10`-frame guard completes at cycle `65669905`
  - the bounded `+progress_every=5000000 +timeout=70000000` run is now the reference longer-motion command on this machine
- Confirmed that the local `data/tmp_probe_16x16_1f.yuv` byte drift is still pre-existing:
  - do not use that clip as a byte-exact ownership gate

## What Remains

- Start the first fractional-pel translational inter ownership slice on the reduced LAST-only path.
- Mirror the writer's latent subpel MV payload support onto the RTL syntax path, then enable the smallest half-pel verification checkpoint.
- Re-run the repaired `7`-frame and `10`-frame exact guards after the first subpel step lands.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate.
- The full `10`-frame `64x64` natural-motion guard needs `+timeout=70000000` or higher on this machine.

## Exact Next Command Or File To Edit

- File to edit: `rtl/av1_encoder_top.v`
- Next command:
  - `rg -n "encode_mv_component|force_integer_mv|TS_SYNTAX_MVCLASS0W|TS_SYNTAX_MVBITW" tb/av1_bitstream_writer.h rtl/av1_encoder_top.v`
