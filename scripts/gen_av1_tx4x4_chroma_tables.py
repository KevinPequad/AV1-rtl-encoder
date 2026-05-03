#!/usr/bin/env python3
from __future__ import annotations

import base64
import subprocess
import tempfile
import urllib.request
from pathlib import Path

TOKEN_CDFS_URL = "https://aomedia.googlesource.com/aom/+/refs/heads/main/av1/common/token_cdfs.h?format=TEXT"

DUMPER_C = r"""
#include <stdio.h>
#include "av1/common/token_cdfs.h"

int main(void) {
    int q, ctx, sym;
    for (q = 0; q < 4; ++q) {
        for (sym = 0; sym < 4; ++sym) {
            printf("eob_multi16_chroma %d %d %u\n", q, sym,
                   av1_default_eob_multi16_cdfs[q][1][0][sym]);
        }
        for (ctx = 0; ctx < 9; ++ctx) {
            printf("eob_extra_chroma4 %d %d 0 %u\n", q, ctx,
                   av1_default_eob_extra_cdfs[q][0][1][ctx][0]);
        }
        for (ctx = 0; ctx < 4; ++ctx) {
            for (sym = 0; sym < 2; ++sym) {
                printf("coeff_base_eob_chroma4 %d %d %d %u\n", q, ctx, sym,
                       av1_default_coeff_base_eob_multi_cdfs[q][0][1][ctx][sym]);
            }
        }
        for (ctx = 0; ctx < 42; ++ctx) {
            for (sym = 0; sym < 3; ++sym) {
                printf("coeff_base_chroma4 %d %d %d %u\n", q, ctx, sym,
                       av1_default_coeff_base_multi_cdfs[q][0][1][ctx][sym]);
            }
        }
        for (ctx = 0; ctx < 21; ++ctx) {
            for (sym = 0; sym < 3; ++sym) {
                printf("coeff_br_chroma4 %d %d %d %u\n", q, ctx, sym,
                       av1_default_coeff_lps_multi_cdfs[q][0][1][ctx][sym]);
            }
        }
    }
    return 0;
}
"""

ENTROPY_H = r"""
#include <stdint.h>
typedef uint16_t aom_cdf_prob;
#define TOKEN_CDF_Q_CTXS 4
#define PLANE_TYPES 2
#define DC_SIGN_CONTEXTS 3
#define TX_SIZES 5
#define TXB_SKIP_CONTEXTS 13
#define EOB_COEF_CONTEXTS 9
#define SIG_COEF_CONTEXTS_EOB 4
#define SIG_COEF_CONTEXTS 42
#define LEVEL_CONTEXTS 21
#define BR_CDF_SIZE 4
#define NUM_BASE_LEVELS 2
#define CDF_SIZE(n) ((n)+1)
#define AOM_ICDF(x) ((uint16_t)(32768u - (unsigned)(x)))
#define AOM_CDF2(a) AOM_ICDF(a),0
#define AOM_CDF3(a,b) AOM_ICDF(a),AOM_ICDF(b),0
#define AOM_CDF4(a,b,c) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),0
#define AOM_CDF5(a,b,c,d) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),0
#define AOM_CDF6(a,b,c,d,e) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),AOM_ICDF(e),0
#define AOM_CDF7(a,b,c,d,e,f) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),AOM_ICDF(e),AOM_ICDF(f),0
#define AOM_CDF8(a,b,c,d,e,f,g) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),AOM_ICDF(e),AOM_ICDF(f),AOM_ICDF(g),0
#define AOM_CDF9(a,b,c,d,e,f,g,h) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),AOM_ICDF(e),AOM_ICDF(f),AOM_ICDF(g),AOM_ICDF(h),0
#define AOM_CDF10(a,b,c,d,e,f,g,h,i) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),AOM_ICDF(e),AOM_ICDF(f),AOM_ICDF(g),AOM_ICDF(h),AOM_ICDF(i),0
#define AOM_CDF11(a,b,c,d,e,f,g,h,i,j) AOM_ICDF(a),AOM_ICDF(b),AOM_ICDF(c),AOM_ICDF(d),AOM_ICDF(e),AOM_ICDF(f),AOM_ICDF(g),AOM_ICDF(h),AOM_ICDF(i),AOM_ICDF(j),0
"""

SPECS = {
    "eob_multi16_chroma": ((4,), 4, 6),
    "eob_extra_chroma4": ((4, 9), 1, 3),
    "coeff_base_eob_chroma4": ((4, 4), 2, 4),
    "coeff_base_chroma4": ((4, 42), 3, 5),
    "coeff_br_chroma4": ((4, 21), 3, 5),
}

CPP_NAMES = {
    "eob_multi16_chroma": "av1_eob_multi16_chroma_cdf_qctx",
    "eob_extra_chroma4": "av1_eob_extra_chroma4_cdf_qctx",
    "coeff_base_eob_chroma4": "av1_coeff_base_eob_chroma4_cdf_qctx",
    "coeff_base_chroma4": "av1_coeff_base_chroma4_cdf_qctx",
    "coeff_br_chroma4": "av1_coeff_br_chroma4_cdf_qctx",
}


def alloc(dims, fill=None):
    if not dims:
        return fill
    return [alloc(dims[1:], fill) for _ in range(dims[0])]


def parse_dump(text: str):
    tables = {name: alloc(dims, None) for name, (dims, _stored, _full) in SPECS.items()}
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        name = parts[0]
        if name == "eob_multi16_chroma":
            q, sym, value = map(int, parts[1:])
            if tables[name][q] is None:
                tables[name][q] = [0] * SPECS[name][2]
            tables[name][q][sym] = value
        elif name in ("eob_extra_chroma4",):
            q, ctx, _sym, value = map(int, parts[1:])
            if tables[name][q][ctx] is None:
                tables[name][q][ctx] = [0] * SPECS[name][2]
            tables[name][q][ctx][0] = value
        else:
            q, ctx, sym, value = map(int, parts[1:])
            if tables[name][q][ctx] is None:
                tables[name][q][ctx] = [0] * SPECS[name][2]
            tables[name][q][ctx][sym] = value
    return tables


def fmt_cpp(v, indent=0):
    pad = " " * indent
    if isinstance(v[0], int):
        return "{" + ",".join(str(x) for x in v) + "}"
    inner = ",\n".join(f"{pad}  {fmt_cpp(x, indent + 2)}" for x in v)
    return "{\n" + inner + f"\n{pad}" + "}"


def verilog_flat(values):
    padded = list(values) + [0] * (16 - len(values))
    return "{" + ",".join(f"16'd{v}" for v in reversed(padded)) + "}"


def render_cpp(tables):
    lines = [
        "// Generated by scripts/gen_av1_tx4x4_chroma_tables.py from AOM token_cdfs.h",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
    ]
    for name, values in tables.items():
        dims, _stored, full = SPECS[name]
        arr = CPP_NAMES[name]
        dim_text = "".join(f"[{d}]" for d in dims)
        lines.append(f"static const uint16_t {arr}{dim_text}[{full}] = {fmt_cpp(values)};")
        lines.append("")
    return "\n".join(lines)


def emit_case_function(lines, fn_name, input_decl, values_by_qctx):
    lines.append(f"function [255:0] {fn_name};")
    for decl in input_decl:
        lines.append(f"    input {decl};")
    lines.append("    begin")
    lines.append("        case (qctx)")
    for qctx, vals in enumerate(values_by_qctx):
        lines.append(f"            2'd{qctx}: begin")
        if input_decl == ["[1:0] qctx"]:
            lines.append(f"                {fn_name} = {verilog_flat(vals)};")
        else:
            lines.append("                case (ctx)")
            for ctx, table in enumerate(vals):
                lines.append(f"                    6'd{ctx}: {fn_name} = {verilog_flat(table)};")
            lines.append(f"                    default: {fn_name} = {verilog_flat(vals[-1])};")
            lines.append("                endcase")
        lines.append("            end")
    default_vals = values_by_qctx[-1]
    if input_decl == ["[1:0] qctx"]:
        lines.append(f"            default: {fn_name} = {verilog_flat(default_vals)};")
    else:
        lines.append(f"            default: {fn_name} = {verilog_flat(default_vals[-1])};")
    lines.append("        endcase")
    lines.append("    end")
    lines.append("endfunction")
    lines.append("")


def render_verilog(tables):
    lines = ["// Generated by scripts/gen_av1_tx4x4_chroma_tables.py from AOM token_cdfs.h", ""]
    emit_case_function(lines, "eob_multi16_chroma_icdf_flat_qctx", ["[1:0] qctx"], tables["eob_multi16_chroma"])
    emit_case_function(lines, "eob_extra_chroma4_icdf_flat_qctx", ["[1:0] qctx", "[5:0] ctx"], tables["eob_extra_chroma4"])
    emit_case_function(lines, "coeff_base_eob_chroma4_icdf_flat_qctx", ["[1:0] qctx", "[5:0] ctx"], tables["coeff_base_eob_chroma4"])
    emit_case_function(lines, "coeff_base_chroma4_icdf_flat_qctx", ["[1:0] qctx", "[5:0] ctx"], tables["coeff_base_chroma4"])
    emit_case_function(lines, "coeff_br_chroma4_icdf_flat_qctx", ["[1:0] qctx", "[5:0] ctx"], tables["coeff_br_chroma4"])
    return "\n".join(lines)


def main():
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="av1_tx4x4_chroma_") as td:
        tmp = Path(td)
        token = base64.b64decode(urllib.request.urlopen(TOKEN_CDFS_URL, timeout=30).read()).decode("utf-8")
        (tmp / "config").mkdir()
        (tmp / "config" / "aom_config.h").write_text("", encoding="ascii")
        (tmp / "av1" / "common").mkdir(parents=True)
        (tmp / "av1" / "common" / "entropy.h").write_text(ENTROPY_H, encoding="ascii")
        (tmp / "av1" / "common" / "token_cdfs.h").write_text(token, encoding="utf-8")
        (tmp / "dump.c").write_text(DUMPER_C, encoding="ascii")
        exe = tmp / "dump"
        subprocess.run(["cc", f"-I{tmp}", str(tmp / "dump.c"), "-o", str(exe)], check=True)
        dump = subprocess.check_output([str(exe)], text=True)
    tables = parse_dump(dump)
    (repo / "tb" / "av1_tx4x4_chroma_tables.h").write_text(render_cpp(tables), encoding="ascii")
    (repo / "rtl" / "av1_tx4x4_chroma_tables.vh").write_text(render_verilog(tables), encoding="ascii")


if __name__ == "__main__":
    main()
