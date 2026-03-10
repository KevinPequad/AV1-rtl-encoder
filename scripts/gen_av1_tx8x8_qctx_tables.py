#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


DUMPER_C = r"""
#include <stdio.h>

#include "av1/common/token_cdfs.h"

int main(void) {
    int q, ctx, plane, sym;

    for (q = 0; q < 4; ++q) {
        for (ctx = 0; ctx < 13; ++ctx) {
            printf("txb_skip_4x4 %d %d 0 %u\n", q, ctx,
                   av1_default_txb_skip_cdfs[q][0][ctx][0]);
            printf("txb_skip_8x8 %d %d 0 %u\n", q, ctx,
                   av1_default_txb_skip_cdfs[q][1][ctx][0]);
        }

        for (plane = 0; plane < 2; ++plane) {
            for (sym = 0; sym < 6; ++sym) {
                printf("eob_multi64 %d %d %d %u\n", q, plane, sym,
                       av1_default_eob_multi64_cdfs[q][plane][0][sym]);
            }
            for (ctx = 0; ctx < 9; ++ctx) {
                printf("eob_extra %d %d %d %u\n", q, plane, ctx,
                       av1_default_eob_extra_cdfs[q][1][plane][ctx][0]);
            }
        }

        for (ctx = 0; ctx < 4; ++ctx) {
            for (sym = 0; sym < 2; ++sym) {
                printf("coeff_base_eob %d %d %d %u\n", q, ctx, sym,
                       av1_default_coeff_base_eob_multi_cdfs[q][1][0][ctx][sym]);
            }
        }

        for (ctx = 0; ctx < 42; ++ctx) {
            for (sym = 0; sym < 3; ++sym) {
                printf("coeff_base %d %d %d %u\n", q, ctx, sym,
                       av1_default_coeff_base_multi_cdfs[q][1][0][ctx][sym]);
            }
        }

        for (ctx = 0; ctx < 21; ++ctx) {
            for (sym = 0; sym < 3; ++sym) {
                printf("coeff_br %d %d %d %u\n", q, ctx, sym,
                       av1_default_coeff_lps_multi_cdfs[q][1][0][ctx][sym]);
            }
        }
    }

    return 0;
}
"""


TABLE_SPECS = {
    "txb_skip_4x4": {"dims": (4, 13), "stored": 1, "full": 3},
    "txb_skip_8x8": {"dims": (4, 13), "stored": 1, "full": 3},
    "eob_multi64": {"dims": (4, 2), "stored": 6, "full": 8},
    "eob_extra": {"dims": (4, 2, 9), "stored": 1, "full": 3},
    "coeff_base_eob": {"dims": (4, 4), "stored": 2, "full": 4},
    "coeff_base": {"dims": (4, 42), "stored": 3, "full": 5},
    "coeff_br": {"dims": (4, 21), "stored": 3, "full": 5},
}


def default_aom_src() -> str:
    return "/home/testuser/aom-official"


def default_aom_build() -> str:
    return "/home/testuser/aom_ref_build"


def run(cmd: list[str], cwd: Path | None = None) -> str:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        check=True,
        text=True,
        capture_output=True,
    )
    return proc.stdout


def alloc_nested(dims: tuple[int, ...], fill):
    if len(dims) == 1:
        return [fill for _ in range(dims[0])]
    return [alloc_nested(dims[1:], fill) for _ in range(dims[0])]


def parse_dump(text: str):
    tables = {}
    for name, spec in TABLE_SPECS.items():
        tables[name] = alloc_nested(spec["dims"], None)

    for raw in text.splitlines():
        parts = raw.strip().split()
        if not parts:
            continue
        name = parts[0]
        spec = TABLE_SPECS[name]
        dims = spec["dims"]
        if name in ("txb_skip_4x4", "txb_skip_8x8"):
            qctx, ctx, _sym, value = map(int, parts[1:])
            slot = tables[name][qctx][ctx]
            if slot is None:
                slot = [0] * spec["full"]
                tables[name][qctx][ctx] = slot
            slot[0] = value
        elif name == "eob_multi64":
            qctx, plane, sym, value = map(int, parts[1:])
            slot = tables[name][qctx][plane]
            if slot is None:
                slot = [0] * spec["full"]
                tables[name][qctx][plane] = slot
            slot[sym] = value
        elif name == "eob_extra":
            qctx, plane, ctx, value = map(int, parts[1:])
            slot = tables[name][qctx][plane][ctx]
            if slot is None:
                slot = [0] * spec["full"]
                tables[name][qctx][plane][ctx] = slot
            slot[0] = value
        elif name == "coeff_base_eob":
            qctx, ctx, sym, value = map(int, parts[1:])
            slot = tables[name][qctx][ctx]
            if slot is None:
                slot = [0] * spec["full"]
                tables[name][qctx][ctx] = slot
            slot[sym] = value
        elif name in ("coeff_base", "coeff_br"):
            qctx, ctx, sym, value = map(int, parts[1:])
            slot = tables[name][qctx][ctx]
            if slot is None:
                slot = [0] * spec["full"]
                tables[name][qctx][ctx] = slot
            slot[sym] = value
        else:
            raise RuntimeError(f"Unhandled dump line: {raw}")

    for name, spec in TABLE_SPECS.items():
        def walk(node, depth=0):
            if depth == len(spec["dims"]):
                if node is None:
                    raise RuntimeError(f"Missing data for {name}")
                return
            for child in node:
                walk(child, depth + 1)
        walk(tables[name])

    return tables


def fmt_cpp(values, indent=0):
    pad = " " * indent
    if isinstance(values[0], int):
        return "{" + ",".join(str(v) for v in values) + "}"
    inner = ",\n".join(f"{pad}  {fmt_cpp(v, indent + 2)}" for v in values)
    return "{\n" + inner + f"\n{pad}" + "}"


def render_cpp_header(tables) -> str:
    lines = []
    lines.append("// Generated by scripts/gen_av1_tx8x8_qctx_tables.py from local AOM token_cdfs.h")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("static inline int av1_coeff_qctx_from_qindex(int qindex) {")
    lines.append("    if (qindex <= 20) return 0;")
    lines.append("    if (qindex <= 60) return 1;")
    lines.append("    if (qindex <= 120) return 2;")
    lines.append("    return 3;")
    lines.append("}")
    lines.append("")
    for name in (
        "txb_skip_4x4",
        "txb_skip_8x8",
        "eob_multi64",
        "eob_extra",
        "coeff_base_eob",
        "coeff_base",
        "coeff_br",
    ):
        arr_name = {
            "txb_skip_4x4": "av1_txb_skip_cdf_4x4_qctx",
            "txb_skip_8x8": "av1_txb_skip_cdf_8x8_qctx",
            "eob_multi64": "av1_eob_multi64_cdf_qctx",
            "eob_extra": "av1_eob_extra_cdf_qctx",
            "coeff_base_eob": "av1_coeff_base_eob_cdf_qctx",
            "coeff_base": "av1_coeff_base_cdf_qctx",
            "coeff_br": "av1_coeff_br_cdf_qctx",
        }[name]
        dims = "".join(f"[{d}]" for d in TABLE_SPECS[name]["dims"])
        full = TABLE_SPECS[name]["full"]
        lines.append(f"static const uint16_t {arr_name}{dims}[{full}] = {fmt_cpp(tables[name])};")
        lines.append("")
    return "\n".join(lines) + "\n"


def verilog_flat(values) -> str:
    padded = list(values) + [0] * (16 - len(values))
    return "{" + ",".join(f"16'd{v}" for v in reversed(padded)) + "}"


def render_verilog_header(tables) -> str:
    lines = []
    lines.append("// Generated by scripts/gen_av1_tx8x8_qctx_tables.py from local AOM token_cdfs.h")
    lines.append("")
    lines.append("function [1:0] coeff_qctx_from_qindex_fn;")
    lines.append("    input [7:0] qindex;")
    lines.append("    begin")
    lines.append("        if (qindex <= 8'd20)")
    lines.append("            coeff_qctx_from_qindex_fn = 2'd0;")
    lines.append("        else if (qindex <= 8'd60)")
    lines.append("            coeff_qctx_from_qindex_fn = 2'd1;")
    lines.append("        else if (qindex <= 8'd120)")
    lines.append("            coeff_qctx_from_qindex_fn = 2'd2;")
    lines.append("        else")
    lines.append("            coeff_qctx_from_qindex_fn = 2'd3;")
    lines.append("    end")
    lines.append("endfunction")
    lines.append("")

    def emit_case_function(name: str, inputs: list[str], body_lines: list[str]):
        lines.append(f"function [255:0] {name};")
        for item in inputs:
            lines.append(f"    input {item};")
        lines.append("    begin")
        lines.extend(f"        {line}" for line in body_lines)
        lines.append("    end")
        lines.append("endfunction")
        lines.append("")

    def qctx_case(values_by_qctx, inner_builder, default_expr):
        body = ["case (qctx)"]
        for qctx, value in enumerate(values_by_qctx):
            body.append(f"2'd{qctx}: begin")
            body.extend(f"    {line}" for line in inner_builder(value))
            body.append("end")
        body.append(f"default: {default_expr};")
        body.append("endcase")
        return body

    emit_case_function(
        "txb_skip_luma_icdf_flat_qctx",
        ["[1:0] qctx", "[3:0] ctx"],
        qctx_case(
            tables["txb_skip_8x8"],
            lambda per_q: [
                "case (ctx)",
                *[
                    f"4'd{ctx}: txb_skip_luma_icdf_flat_qctx = {verilog_flat(values)};"
                    for ctx, values in enumerate(per_q)
                ],
                f"default: txb_skip_luma_icdf_flat_qctx = {verilog_flat(per_q[-1])};",
                "endcase",
            ],
            f"txb_skip_luma_icdf_flat_qctx = {verilog_flat(tables['txb_skip_8x8'][3][-1])}",
        ),
    )

    emit_case_function(
        "txb_skip_chroma_icdf_flat_qctx",
        ["[1:0] qctx", "[3:0] ctx"],
        qctx_case(
            tables["txb_skip_4x4"],
            lambda per_q: [
                "case (ctx)",
                *[
                    f"4'd{ctx}: txb_skip_chroma_icdf_flat_qctx = {verilog_flat(values)};"
                    for ctx, values in enumerate(per_q)
                ],
                f"default: txb_skip_chroma_icdf_flat_qctx = {verilog_flat(per_q[-1])};",
                "endcase",
            ],
            f"txb_skip_chroma_icdf_flat_qctx = {verilog_flat(tables['txb_skip_4x4'][3][-1])}",
        ),
    )

    emit_case_function(
        "eob_multi64_icdf_flat_qctx",
        ["[1:0] qctx", "plane"],
        qctx_case(
            tables["eob_multi64"],
            lambda per_q: [
                "if (plane)",
                f"    eob_multi64_icdf_flat_qctx = {verilog_flat(per_q[1])};",
                "else",
                f"    eob_multi64_icdf_flat_qctx = {verilog_flat(per_q[0])};",
            ],
            f"eob_multi64_icdf_flat_qctx = {verilog_flat(tables['eob_multi64'][3][0])}",
        ),
    )

    emit_case_function(
        "eob_extra_ctx_icdf_flat_qctx",
        ["[1:0] qctx", "plane", "[3:0] ctx"],
        qctx_case(
            tables["eob_extra"],
            lambda per_q: [
                "case ({plane, ctx})",
                *[
                    f"5'b0{ctx:04b}: eob_extra_ctx_icdf_flat_qctx = {verilog_flat(per_q[0][ctx])};"
                    for ctx in range(9)
                ],
                *[
                    f"5'b1{ctx:04b}: eob_extra_ctx_icdf_flat_qctx = {verilog_flat(per_q[1][ctx])};"
                    for ctx in range(9)
                ],
                f"default: eob_extra_ctx_icdf_flat_qctx = {verilog_flat(per_q[0][-1])};",
                "endcase",
            ],
            f"eob_extra_ctx_icdf_flat_qctx = {verilog_flat(tables['eob_extra'][3][0][-1])}",
        ),
    )

    emit_case_function(
        "coeff_base_eob_ctx_icdf_flat_qctx",
        ["[1:0] qctx", "[2:0] ctx"],
        qctx_case(
            tables["coeff_base_eob"],
            lambda per_q: [
                "case (ctx)",
                *[
                    f"3'd{ctx}: coeff_base_eob_ctx_icdf_flat_qctx = {verilog_flat(values)};"
                    for ctx, values in enumerate(per_q)
                ],
                f"default: coeff_base_eob_ctx_icdf_flat_qctx = {verilog_flat(per_q[-1])};",
                "endcase",
            ],
            f"coeff_base_eob_ctx_icdf_flat_qctx = {verilog_flat(tables['coeff_base_eob'][3][-1])}",
        ),
    )

    emit_case_function(
        "coeff_base_ctx_icdf_flat_qctx",
        ["[1:0] qctx", "[5:0] ctx"],
        qctx_case(
            tables["coeff_base"],
            lambda per_q: [
                "case (ctx)",
                *[
                    f"6'd{ctx}: coeff_base_ctx_icdf_flat_qctx = {verilog_flat(values)};"
                    for ctx, values in enumerate(per_q)
                ],
                f"default: coeff_base_ctx_icdf_flat_qctx = {verilog_flat(per_q[-1])};",
                "endcase",
            ],
            f"coeff_base_ctx_icdf_flat_qctx = {verilog_flat(tables['coeff_base'][3][-1])}",
        ),
    )

    emit_case_function(
        "coeff_br_ctx_icdf_flat_qctx",
        ["[1:0] qctx", "[4:0] ctx"],
        qctx_case(
            tables["coeff_br"],
            lambda per_q: [
                "case (ctx)",
                *[
                    f"5'd{ctx}: coeff_br_ctx_icdf_flat_qctx = {verilog_flat(values)};"
                    for ctx, values in enumerate(per_q)
                ],
                f"default: coeff_br_ctx_icdf_flat_qctx = {verilog_flat(per_q[-1])};",
                "endcase",
            ],
            f"coeff_br_ctx_icdf_flat_qctx = {verilog_flat(tables['coeff_br'][3][-1])}",
        ),
    )

    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--aom-src", default=default_aom_src())
    parser.add_argument("--aom-build", default=default_aom_build())
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    scripts_dir = repo_root / "scripts"
    tb_header = repo_root / "tb" / "av1_tx8x8_qctx_tables.h"
    rtl_header = repo_root / "rtl" / "av1_tx8x8_qctx_tables.vh"

    with tempfile.TemporaryDirectory(prefix="av1_qctx_") as tmpdir:
        tmp = Path(tmpdir)
        c_path = tmp / "dump_qctx_tables.c"
        exe_path = tmp / "dump_qctx_tables"
        c_path.write_text(DUMPER_C, encoding="ascii")
        run(
            [
                "cc",
                f"-I{args.aom_src}",
                f"-I{args.aom_build}",
                str(c_path),
                "-o",
                str(exe_path),
            ],
            cwd=scripts_dir,
        )
        dump = run([str(exe_path)], cwd=scripts_dir)

    tables = parse_dump(dump)
    tb_header.write_text(render_cpp_header(tables), encoding="ascii")
    rtl_header.write_text(render_verilog_header(tables), encoding="ascii")


if __name__ == "__main__":
    main()
