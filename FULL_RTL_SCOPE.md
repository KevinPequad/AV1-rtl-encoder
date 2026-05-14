# AV1 RTL Full Scope Matrix

Current program source of truth:
- active goal: full feature-complete RTL AV1
- historical reduced pre-ASIC freeze docs: `PRE_ASIC_HANDOFF.md` and `P7_REFERENCE_BOUNDARY.md`
- source inputs: `av1-reference-docs/svt-av1-feature-inventory.md` and `/home/chudpc/.hermes/kanban/workspaces/t_f3708c82/av1_feature_gap_matrix.md`

This matrix is the authoritative bridge between the SVT-AV1 feature inventory and the repo's current gap audit. The freeze on canonical `main` at `8994490d209a745fb157e999af616b49de2c6ce1` is historical; it is not the endpoint of the project.

## Inventory map

- P0: cross-cutting program governance, doc truth, and merge hygiene.
- P1: inventory sections 1, 2, and 9 (pipeline, sequence/input/output, metadata/signaling).
- P2: inventory section 4 (rate control and quality control).
- P3: inventory sections 5.1, 5.2, and 5.5 (block structure, intra, transform/RD).
- P4: inventory sections 5.1 and 5.5 (partitions and transform/quant/recon).
- P5: inventory section 5.5 (luma coefficient syntax).
- P6: inventory sections 5.2, 5.3, and 5.5 (chroma residual and Cb/Cr syntax).
- P7: inventory sections 3, 5.3, 5.4, and 7 (GOP, inter, ME, stream features).
- P8: inventory sections 3 and 7 (reference frames and GOP/session control).
- P9: inventory section 6 (filters and post-processing).
- P10: inventory section 4 (rate control, AQ, and mode decision).
- P11: inventory sections 2 and 9 (I/O formats, resolution, metadata, final target packaging).
- P12: inventory sections 7, 8, and 9 (advanced AV1 tools, screen-content, signaling).
- P13: post-feature ASIC readiness; outside the SVT feature inventory proper.

## Full-scope matrix

| Row | Scope area | SVT inventory anchors | Current repo state | Full AV1 target | Gap / proof gate |
|---|---|---|---|---|---|
| P0 | Repo baseline, ownership discipline, artifact hygiene | Cross-cutting | Canonical main is frozen at `8994490d209a745fb157e999af616b49de2c6ce1`; worktrees isolate feature work; historical freeze docs exist | One source of truth for active scope, clean serialized merges, and no testbench-repair-based truth changes | README and PROJECT_PLAN must link this file; historical freeze docs stay historical |
| P1 | Bitstream / OBU / sequence and frame headers / tile framing | Inventory 1, 2, 9 | Reduced headers, raw OBU/IVF capture, and a static-CDF path exist; full tiles and adaptive frame-context updates do not | Complete sequence/frame/tile/header ownership, adaptive CDF, segmentation, delta-q, metadata, and packetization | Raw OBU/IVF equality plus FFmpeg/aomdec parity on the active clip set |
| P2 | Entropy coding and CDF ownership | Inventory 1 and 4 | AV1-style range coding exists for the current bool/literal/generic symbols; static-CDF is still the operating point | Complete syntax-CDF coverage with real adaptive update / refresh semantics | `entropy-check` plus every new context gate must stay decoder-clean |
| P3 | Intra prediction and keyframe block syntax | Inventory 5.1, 5.2, 5.5 | 8-bit 4:2:0 intra bring-up exists with DC, directional, Smooth, and Paeth in the current subset | Add CfL, filter intra, recursive search, and broader intra mode coverage | Natural keyframe and intra-probe clips must decode cleanly and match recon |
| P4 | Partitions, block sizes, transform, quant, recon | Inventory 5.1 and 5.5 | 64x64 superblocks, fixed 8x8 coding blocks, TX_8X8 luma, and TX_4X4 chroma exist in the current subset | Full square/non-square partitions, smaller/larger transform choices, transform-type search, qindex=0 lossless, and delta-q | Partition/transform sweep clips must pass raw-byte and decoder parity |
| P5 | Luma coefficient syntax | Inventory 5.5 | Reduced DC-only and low-order AC coefficient paths exist for the current subset | Complete coefficient syntax across all coefficient distributions, EOB ranges, transform sizes, and transform types | Coefficient probe clips across q ranges must stay decoder-valid |
| P6 | Chroma residual and Cb/Cr syntax | Inventory 5.2, 5.3, 5.5 | Chroma residual core, chroma inter prediction, TX_4X4 tables, and current syntax gates exist | Broader natural chroma, multi-frame inter residuals, CfL, and robust current/refframe chroma-neighbor ownership | Non-flat chroma probes must pass raw-byte and FFmpeg/aomdec parity |
| P7 | Inter prediction, ME, q3/fractional motion, MV syntax | Inventory 3, 5.3, 5.4, 7 | Reduced LAST-only inter with q3 / 2D half-pel cores and controlled natural32 / natural64 gates exist | Compound refs, all MV predictor classes, MFMV, OBMC, warped/global motion, inter-intra, wedge, weighted compound, and multi-ref coverage | Natural-motion public-decoder parity plus MV summary checks must pass |
| P8 | Reference frames, picture types, and GOP/session control | Inventory 3 and 7 | Low-delay I/P path, LAST-only refresh, and longer reduced-motion guards exist | Full reference-frame lifecycle, random access, open/closed GOP, overlays, hierarchical layers, and order hints | Longer-sequence and GOP control gates must validate the active scope |
| P9 | Loop filter, CDEF, restoration, reconstructed-frame ownership | Inventory 6 | Filters are disabled in the frozen checkpoint | Deblock, CDEF, restoration, and filtered reference ownership for the full AV1 program | Filter-enabled clips must prove recon parity; this lane is active-scope work, not the historical freeze |
| P10 | Rate control, quality control, mode decision, search quality | Inventory 4 | Fixed-QP / simple RC bring-up and limited search quality exist | CQP, CRF, VBR, CBR, AQ, ROI, recode loops, quant matrices, and psychovisual controls | Bitrate / quality regressions on representative clips must stay within target |
| P11 | Input/output formats, resolution scaling, final target | Inventory 2 and 9 | 8-bit 4:2:0 raw YUV plus small fixtures exist | Big Buck Bunny `1280x720 @ 24 fps` representative clip from the RTL byte path, plus packaging / metadata | A 5-10 frame full-resolution low-delay clip must decode publicly and match/track RTL recon with meaningful inter/reference behavior; 240 frames is optional soak evidence, not a required proof gate |
| P12 | Advanced AV1 tools | Inventory 7, 8, 9 | Screen-content and advanced AV1 tools are deferred | Screen-content, IBC, palette, and the remaining advanced AV1 tools that the full program keeps in scope | Any enabled tool must have standalone RTL gates plus public-decoder gates |
| P13 | ASIC readiness | Post-feature only | No ASIC synthesis / signoff evidence exists | Lint, synthesis, memory macro strategy, timing, P&R, gate-level, and power/area closure | ASIC work starts only after P0-P12 are actually green |

## Bottom line

The active backlog is full feature-complete RTL AV1. The reduced pre-ASIC freeze is a historical checkpoint only, and the freeze docs must never be read as the endpoint of the project. Use this matrix as the active source of truth; use the historical freeze docs only as evidence for the reduced checkpoint. Proof gates should be sized to prove behavior efficiently: prefer 5-10 representative frames for full-resolution inter/BBB validation, and treat 240-frame runs as optional soak/regression evidence rather than a hard completion requirement.
