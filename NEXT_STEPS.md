# Next Steps

## Completed

- Routed zero-motion inter blocks with coefficients through the real generic coefficient syntax path in `rtl/av1_encoder_top.v`.
- Switched the RTL inter `tx_type` symbol emission to the correct inter `DCT_DCT` CDF instead of the intra table.
- Verified the first larger natural-content repeated-frame inter guard at `output/natural_repeat64_x640_y360_q128_fix1/`:
  - `encoded.obu` matches `encoded_rtl_raw.obu`
  - `encoded.ivf` matches `encoded_rtl.ivf`
  - decoded RTL IVF matches `recon.yuv`
- Confirmed that the local `data/tmp_probe_16x16_1f.yuv` byte drift is pre-existing:
  - a temporary revert of the new inter-path patch still produces the same `34` vs `35` byte mismatch
  - do not treat that clip as a regression caused by this slice

## What Remains

- Implement reduced raw-path `NEARESTMV` / `NEWMV` mode signaling for the current single-reference inter subset.
- Add MV payload syntax after the inter mode symbols are exact.
- Restore or recover `data/ac_probe_16x16_1f.yuv` in this checkout so the documented exact-match `16x16` ownership gate can be rerun locally.

## Blockers

- `data/ac_probe_16x16_1f.yuv` is missing from this checkout.
- `data/tmp_probe_16x16_1f.yuv` is not a byte-exact substitute ownership gate; software vs RTL first differ at byte `14` even with the new inter-path patch temporarily reverted.

## Exact Next Command Or File To Edit

- File to edit: `rtl/av1_encoder_top.v`
- Next command:
  - `wsl bash -lc 'cd /mnt/c/Users/pie/Desktop/av1stage64/tb && make VERILATOR=/home/testuser/.local/bin/verilator THREADS=24 BUILD_JOBS=24 WIDTH=64 HEIGHT=64 >/tmp/av1_nearestmv_build.log 2>&1 && ./obj_dir/Vav1_encoder_top +input=/mnt/c/Users/pie/Desktop/Encdoing\\ Shenanigans/AV1-rtl-encoder/data/natural_repeat64_x640_y360_2f.yuv +output=/mnt/c/Users/pie/Desktop/Encdoing\\ Shenanigans/AV1-rtl-encoder/output/natural_repeat64_x640_y360_nearestmv/encoded.obu +frames=2 +all_key=0 +qindex=128 +dc_only=0 +dump_inter_summary=1 >/tmp/av1_nearestmv_run.log 2>&1 && tail -n 40 /tmp/av1_nearestmv_run.log'`
