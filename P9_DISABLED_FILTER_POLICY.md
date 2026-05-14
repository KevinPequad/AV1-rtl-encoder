# P9 Disabled Filter / Reference Ownership Policy

This low-delay AV1 RTL subset intentionally keeps all post-reconstruction in-loop filters disabled:

- Sequence header: `enable_cdef = 0`
- Sequence header: `enable_restoration = 0`
- Frame headers: `loop_filter_level[0] = 0` and `loop_filter_level[1] = 0`
- Frame headers: no chroma loop-filter levels are emitted, because both luma levels are zero

Under that contract, the AV1 post-filter/reference frame is byte-identical to the unfiltered reconstructed frame for the current no-superres/no-film-grain subset. Therefore the RTL-owned luma/chroma reconstructed blocks are intentionally written directly into the current reference memories, and the C++ harness promotes those current reconstructed buffers to the next frame's LAST-reference buffers.

Guardrails added for this policy:

- `rtl/av1_bitstream.v` names the P9 disabled-filter constants used for `enable_cdef`, `enable_restoration`, and loop-filter parameters.
- `tb/test_rtl_bitstream.cpp` now parses the emitted sequence/key/inter headers and fails if CDEF, restoration, or non-zero loop-filter levels appear. It also contains negative guard cases for filter-enabled expected headers.
- `tb/tb_av1_encoder.cpp` has a plusarg-independent P9 invariant before `recon.yuv` dump and reference promotion, making direct unfiltered promotion conditional on this disabled-filter policy.

Do not make the testbench repair filtered references. If a future lane enables loop filtering, CDEF, or restoration, the encoder must add real RTL post-reconstruction filter/restoration writeback before dumping `recon.yuv` or promoting reference buffers.

## Validation record

- Canonical proof commit: `55850d927f4f6018bcc00d7556fef2029d2404c7` (`origin/main` at validation time).
- Validation worktree: `/tmp/t_3754b875_clean`.
- Repo status when this note was written: local `main` is `11021fac2ca7644e3acda4e38931663bea4cb49a` and is ahead of `origin/main` by 1 commit.
- Gates passed under `THREADS=1 BUILD_JOBS=1`:
  - `make -C tb bitstream-check`
  - `make -C tb top-public-matrix-check`
  - `make -C tb standalone-matrix-check`
- Proof result:
  - sequence headers keep `enable_cdef=0` and `enable_restoration=0`
  - frame headers keep `loop_filter_level[0..1]=0`, so no chroma loop-filter levels are emitted
  - the harness promotes reconstructed luma/chroma buffers directly into LAST because post-reconstruction filtering is intentionally absent
  - `top-public-matrix-check` proved the 32x32 natural-ish fractional NEWMV path, and both FFmpeg/libdav1d and `aomdec` decoded the RTL IVF back to `recon.yuv`
- Deferred tools:
  - if loop filter, CDEF, restoration, superres, or film grain are re-enabled later, the RTL must add real post-reconstruction writeback before `recon.yuv` dump or reference promotion
  - the C++ testbench must not repair filtered references
