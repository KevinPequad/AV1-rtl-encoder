#!/usr/bin/env python3
"""Capped Yosys frontend smoke for individual AV1 RTL leaves.

Full top is still too heavy for the current capped smoke. This keeps ASIC
progress measurable by proving leaves can parse/lower through Yosys and by
pinpointing the next synthesis-hostile block without an uncapped full run.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

DEFAULT_MODULES = [
    "av1_bitstream",
    "av1_chroma_inter_pred",
    "av1_chroma_residual",
    "av1_entropy",
    "av1_fetch",
    "av1_inter_pred",
    "av1_intra_pred",
    "av1_inverse_quant",
    "av1_inverse_transform",
    "av1_me",
    "av1_quantize",
    "av1_reconstruct",
    "av1_transform",
]

RTL_BY_MODULE = {p.stem: p for p in Path("rtl").glob("*.v")}
DEPENDENCIES = {
    "av1_chroma_residual": [
        "av1_transform",
        "av1_quantize",
        "av1_inverse_quant",
        "av1_inverse_transform",
    ],
    "av1_encoder_top": [m for m in RTL_BY_MODULE if m != "av1_encoder_top"],
}


def module_files(module: str) -> list[Path]:
    seen: set[str] = set()
    ordered: list[Path] = []

    def add(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        for dep in DEPENDENCIES.get(name, []):
            add(dep)
        rtl = RTL_BY_MODULE.get(name)
        if rtl is not None:
            ordered.append(rtl)

    add(module)
    return ordered


def run_module(module: str, timeout_s: int, log_dir: Path) -> tuple[str, str]:
    files = module_files(module)
    if not files:
        return "MISSING", f"no rtl/{module}.v"
    file_args = " ".join(str(p) for p in files)
    script = "\n".join([
        f"read_verilog -sv -Irtl {file_args}",
        f"hierarchy -check -top {module}",
        "proc",
        "opt",
        "stat",
    ]) + "\n"
    log_path = log_dir / f"{module}.log"
    try:
        proc = subprocess.run(
            ["yosys", "-q", "-p", script],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout or ""
        if isinstance(out, bytes):
            out = out.decode("utf-8", errors="replace")
        log_path.write_text(out + f"\n[TIMEOUT] {timeout_s}s\n", encoding="utf-8")
        return "TIMEOUT", str(log_path)
    log_path.write_text(proc.stdout or "", encoding="utf-8")
    return ("PASS" if proc.returncode == 0 else "FAIL"), str(log_path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--log-dir", default="build/asic/leaf_sweep")
    ap.add_argument("--module", action="append", dest="modules")
    args = ap.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    modules = args.modules or DEFAULT_MODULES

    if subprocess.run(["bash", "-lc", "command -v yosys >/dev/null"], check=False).returncode != 0:
        print("Yosys not installed; skipping leaf sweep")
        return 0

    failed = False
    for module in modules:
        status, detail = run_module(module, args.timeout, log_dir)
        print(f"[{status}] {module}: {detail}")
        failed = failed or status != "PASS"
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
