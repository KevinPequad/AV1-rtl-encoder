# AV1 Pre-ASIC Handoff

Canonical freeze baseline:
- commit `8994490d209a745fb157e999af616b49de2c6ce1` on `main`
- `origin/main` is pushed
- validated on Chud PC 2 with `THREADS=1 BUILD_JOBS=1`
- current matrix: 21/21 gates passed
- proof set: raw OBU equality, raw IVF equality, FFmpeg/libdav1d parity, aomdec parity, and no testbench or packaging repair masking RTL bugs

Supported frozen baseline:

- 8-bit 4:2:0 low-delay encode/decode flow
- 16x16 smoke ownership and byte-path correctness
- luma intra search across DC, directional, SMOOTH, and PAETH
- luma transform, quantization, inverse transform, and reconstruction
- reduced single-reference LAST inter
- integer-pel MVs plus reduced LAST-path motion syntax
- q3 motion-estimation / inter predictor / chroma residual integration
- P7 LAST-path motion modes on the reduced LAST-only path: GLOBALMV, NEARESTMV, NEARMV, NEWMV, ZEROMV
- current public-decoder proof surface: `S0`-`S16` and `T0`-`T9` from `scripts/run_av1_regression_matrix.py`

Excluded / deferred lanes:

- LAST2, LAST3, GOLDEN, BWDREF, ALTREF, ALTREF2
- compound prediction of any family, including weighted, wedge, and masked variants
- MFMV / `use_ref_frame_mvs`
- OBMC
- warped/global motion beyond the reduced signaling
- inter-intra
- random access, open GOP, overlay, hierarchical GOP, and multi-ref refresh policy
- post-reconstruction filters: CDEF, restoration, superres, film grain
- the final `1280x720 @ 24 fps` Big Buck Bunny target and any broader feature expansion beyond the frozen baseline

Regression commands:

- `THREADS=1 BUILD_JOBS=1 python3 scripts/run_av1_regression_matrix.py --outdir /tmp/av1_matrix_pre_asic --timeout-seconds 1200 --keep-going`
- `make THREADS=1 BUILD_JOBS=1 entropy-check`
- `make THREADS=1 BUILD_JOBS=1 bitstream-check WIDTH=16 HEIGHT=16`
- `make THREADS=1 BUILD_JOBS=1 natural32-ip-syntax-check`
- `make THREADS=1 BUILD_JOBS=1 natural32-ip-newmv-syntax-check`
- `make THREADS=1 BUILD_JOBS=1 natural32-ip-fractional-syntax-check`
- `make THREADS=1 BUILD_JOBS=1 gop-lifecycle-syntax-check`
- `make THREADS=1 BUILD_JOBS=1 natural64-ip-mode-context-syntax-check`
- `make THREADS=1 BUILD_JOBS=1 natural64-ip-fractional-syntax-check`
- `make THREADS=1 BUILD_JOBS=1 rtl-byte-owner-check`
- `make THREADS=1 BUILD_JOBS=1 chudpc2-smoke`

Commit IDs:

- canonical freeze baseline: `8994490d209a745fb157e999af616b49de2c6ce1`
- P7 boundary lineage: `59ba7fd55f47ac899c8d2c39e061a4b2e13aefd2` -> `ca1a65ff0b850f60769a4f000de6d5ce0dbcf3d0`
- earlier post-merge docs checkpoint: `6fb59b4192f3123bd83e93c4070b2e2a88ff02df`

ASIC-readiness blockers:

- no ASIC synthesis / lint / timing / P&R / signoff flow yet
- no memory-macro / black-box strategy validated for a full ASIC path
- no packaging / power / area closure evidence yet
- `scripts/run_av1_regression_matrix.py` still lists P13 ASIC gates as skipped placeholders

Handoff note:

- This is a freeze marker, not a new feature lane. Future regressions should be fixed against the frozen baseline, and no new codec feature work should start without an explicit scope reset.
