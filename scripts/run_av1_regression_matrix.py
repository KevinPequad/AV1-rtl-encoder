#!/usr/bin/env python3
"""Run AV1 regression matrix gates and write JSON/Markdown evidence manifests.

This runner is validation infrastructure only. It records current boundary
passes/failures and lists future P0-P13 feature-complete gates as skipped until
feature-lane tasks implement them; skipped is never reported as pass.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import datetime as _dt
import hashlib
import json
import os
import shlex
import subprocess
import sys
import time
from typing import Iterable

REPO = Path(__file__).resolve().parents[1]
TB = REPO / "tb"
DEFAULT_THREADS = "1"


@dataclass(frozen=True)
class Gate:
    gate_id: str
    name: str
    command: tuple[str, ...]
    cwd: Path
    public_decoder: bool = False
    description: str = ""


def make_cmd(*args: str) -> tuple[str, ...]:
    return ("make", f"THREADS={DEFAULT_THREADS}", f"BUILD_JOBS={DEFAULT_THREADS}", *args)


def _sanitize_tag(text: str) -> str:
    return "".join(ch if ch.isalnum() else "_" for ch in text).strip("_") or "matrix"


def gate_namespace(run_namespace: str, gate: Gate) -> str:
    return _sanitize_tag(f"{run_namespace}_{gate.gate_id}_{gate.name}")[:80]


def gate_command(run_namespace: str, gate: Gate) -> list[str]:
    argv = [str(x) for x in gate.command]
    if not argv or argv[0] != "make":
        return argv
    ns = gate_namespace(run_namespace, gate)
    overrides = [
        f"OBJ_DIR=obj_dir_{ns}",
        f"TRACE_OBJ_DIR=obj_dir_trace_{ns}",
        f"SIM_BIN=Vav1_encoder_top_{ns}",
        f"TRACE_SIM_BIN=Vav1_encoder_top_trace_{ns}",
        f"ENTROPY_OBJ_DIR=obj_dir_entropy_{ns}",
        f"ENTROPY_SIM_BIN=Vav1_entropy_{ns}",
        f"BITSTREAM_OBJ_DIR=obj_dir_bitstream_{ns}",
        f"BITSTREAM_SIM_BIN=Vav1_bitstream_{ns}",
        f"INV_XFORM_OBJ_DIR=obj_dir_inv_xform_{ns}",
        f"INV_XFORM_SIM_BIN=Vav1_inverse_transform_{ns}",
        f"INTER_OBJ_DIR=obj_dir_inter_pred_{ns}",
        f"INTER_SIM_BIN=Vav1_inter_pred_{ns}",
        f"ME_OBJ_DIR=obj_dir_me_{ns}",
        f"ME_SIM_BIN=Vav1_me_{ns}",
        f"CHROMA_INTER_OBJ_DIR=obj_dir_chroma_inter_pred_{ns}",
        f"CHROMA_INTER_SIM_BIN=Vav1_chroma_inter_pred_{ns}",
        f"CHROMA_RES_OBJ_DIR=obj_dir_chroma_residual_{ns}",
        f"CHROMA_RES_SIM_BIN=Vav1_chroma_residual_{ns}",
        f"CHROMA_COEFF_TABLE_BIN=test_chroma_coeff_tables_{ns}",
    ]
    return [argv[0], *overrides, *argv[1:]]


def gate_env(run_namespace: str, gate: Gate, base_env: dict[str, str]) -> dict[str, str]:
    env = base_env.copy()
    ns = gate_namespace(run_namespace, gate)
    env["AV1_TOP_SIM"] = f"./obj_dir_{ns}/Vav1_encoder_top_{ns}"
    return env


CURRENT_GATES: list[Gate] = [
    Gate("S0", "clean", make_cmd("clean"), TB, False, "stale artifact hygiene"),
    Gate("S1", "entropy-check", make_cmd("entropy-check"), TB, False, "AV1 entropy core standalone"),
    Gate("S2", "bitstream-check", make_cmd("WIDTH=16", "HEIGHT=16", "bitstream-check"), TB, False, "reduced header/OBU bitstream standalone"),
    Gate("S3", "inv-xform-check", make_cmd("inv-xform-check"), TB, False, "inverse transform standalone"),
    Gate("S4", "inter-pred-check", make_cmd("WIDTH=32", "HEIGHT=32", "inter-pred-check"), TB, False, "luma q3 inter predictor standalone"),
    Gate("S5", "chroma-inter-pred-check", make_cmd("WIDTH=32", "HEIGHT=32", "chroma-inter-pred-check"), TB, False, "chroma q4/q3 inter predictor standalone"),
    Gate("S6", "me-check", make_cmd("WIDTH=32", "HEIGHT=32", "me-check"), TB, False, "q3 motion-estimation standalone"),
    Gate("S7", "chroma-residual-check", make_cmd("chroma-residual-check"), TB, False, "chroma residual standalone"),
    Gate("S8", "chroma-coeff-table-check", make_cmd("chroma-coeff-table-check"), TB, False, "AOM-derived chroma coefficient tables"),
    Gate("S9", "top-chroma-integration-check", make_cmd("top-chroma-integration-check"), TB, False, "static top-level chroma integration guard"),
    Gate("S16", "rtl-byte-owner-check", make_cmd("rtl-byte-owner-check"), TB, True, "positive RTL ownership plus N1-N5 anti-repair probes"),
    Gate("T0", "chudpc2-smoke", ("bash", "scripts/run_av1_top_smoke.sh"), REPO, True, "16x16 all-key/IP plus GOP-boundary Chud PC 2 public decoder smoke"),
    Gate("T1", "nonzero-chroma-syntax-check", make_cmd("nonzero-chroma-syntax-check"), TB, True, "8x8 non-zero Cb/Cr public decoder proof"),
    Gate("T2", "nonzero-chroma16-syntax-check", make_cmd("nonzero-chroma16-syntax-check"), TB, True, "16x16 non-zero chroma public decoder proof"),
    Gate("T3", "natural32-chroma-syntax-check", make_cmd("natural32-chroma-syntax-check"), TB, True, "32x32 natural-ish all-key public decoder proof"),
    Gate("T4", "natural32-ip-syntax-check", make_cmd("natural32-ip-syntax-check"), TB, True, "32x32 zero-MV IP public decoder proof"),
    Gate("T5", "natural32-ip-newmv-syntax-check", make_cmd("natural32-ip-newmv-syntax-check"), TB, True, "32x32 isolated NEWMV public decoder proof"),
    Gate("T6", "natural32-ip-fractional-syntax-check", make_cmd("natural32-ip-fractional-syntax-check"), TB, True, "32x32 fractional q3 NEWMV public decoder proof"),
    Gate("T7", "gop-lifecycle-syntax-check", make_cmd("gop-lifecycle-syntax-check"), TB, True, "64x64 low-delay LAST-only GOP lifecycle proof"),
    Gate("T8", "natural64-ip-mode-context-syntax-check", make_cmd("natural64-ip-mode-context-syntax-check"), TB, True, "64x64 LAST-path GLOBALMV/NEARESTMV/NEARMV/NEWMV syntax gate"),
    Gate("T9", "natural64-ip-fractional-syntax-check", make_cmd("natural64-ip-fractional-syntax-check"), TB, True, "64x64 fractional q3 NEWMV public decoder proof"),
    Gate("T10", "p5-highdc-q1-public-check", make_cmd("p5-highdc-q1-public-check"), TB, True, "16x16 qindex=1 high-DC / non-zero AC public decoder proof"),
    Gate("T11", "natural64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural64-ip-fullcoeff-newmv-syntax-check"), TB, True, "64x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T12", "natural64-ip-fullcoeff-newmv-boundary45-probe-check", make_cmd("natural64-ip-fullcoeff-newmv-boundary45-probe-check"), TB, True, "64x64 boundary-45 full-coeff ref-stack regression public decoder proof"),
    Gate("T13", "natural64x128-ip-fullcoeff-newmv-syntax-check", make_cmd("natural64x128-ip-fullcoeff-newmv-syntax-check"), TB, True, "64x128 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T14", "natural128x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural128x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "128x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T15", "natural192x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural192x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "192x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T16", "natural256x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural256x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "256x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T17", "natural320x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural320x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "320x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T18", "natural384x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural384x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "384x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T19", "natural448x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural448x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "448x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T20", "natural512x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural512x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "512x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T21", "natural576x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural576x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "576x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T22", "natural640x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural640x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "640x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T23", "natural704x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural704x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "704x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T24", "natural768x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural768x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "768x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T25", "natural832x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural832x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "832x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T26", "natural896x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural896x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "896x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T27", "natural960x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural960x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "960x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T28", "natural1024x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural1024x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "1024x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T29", "natural1088x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural1088x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "1088x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T30", "natural1152x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural1152x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "1152x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
    Gate("T31", "natural1216x64-ip-fullcoeff-newmv-syntax-check", make_cmd("natural1216x64-ip-fullcoeff-newmv-syntax-check"), TB, True, "1216x64 full-coeff LAST reference-stack/NEWMV public decoder proof"),
]

FUTURE_GATES: list[dict[str, str]] = [
    {"audit_row": "P0", "name": "baseline-hygiene-check", "reason": "manifest-only placeholder until a dedicated hygiene gate is implemented; not a pass"},
    {"audit_row": "P0", "name": "artifact-manifest-check", "reason": "runner emits manifests, but a dedicated artifact-audit gate is still pending; not a pass"},
    {"audit_row": "P1", "name": "headers-syntax-check", "reason": "feature-lane implementation pending; not a pass"},
    {"audit_row": "P1", "name": "obu-size-backpatch-check", "reason": "feature-lane implementation pending; not a pass"},
    {"audit_row": "P1", "name": "tile-info-check", "reason": "tile/header coverage beyond current subset is pending; not a pass"},
    {"audit_row": "P2", "name": "cdf-static-coverage-check", "reason": "feature-lane implementation pending; not a pass"},
    {"audit_row": "P2", "name": "adaptive-cdf-negative-check", "reason": "adaptive-CDF ownership is not implemented yet; not a pass"},
    {"audit_row": "P3", "name": "natural64-keyframe-check", "reason": "64x64 keyframe/intra coverage pending; not a pass"},
    {"audit_row": "P3", "name": "directional-intra-probe-check", "reason": "broader intra predictor coverage pending; not a pass"},
    {"audit_row": "P4", "name": "partition-shape-public-check", "reason": "partition/transform public proof pending; not a pass"},
    {"audit_row": "P4", "name": "qindex-sweep-public-check", "reason": "qindex/tx-size sweep public proof pending; not a pass"},
    {"audit_row": "P5", "name": "luma-coeff-eob-sweep-check", "reason": "broader luma coefficient / EOB coverage beyond the current large-DC proof is pending; not a pass"},
    {"audit_row": "P5", "name": "lossless-qindex0-tx4-check", "reason": "deferred qindex=0 lossless / TX_4X4 path is not implemented yet; not a pass"},
    {"audit_row": "P6", "name": "natural64-chroma-syntax-check", "reason": "feature-lane implementation pending; not a pass"},
    {"audit_row": "P6", "name": "chroma-ip-residual-check", "reason": "multi-frame chroma inter residual public proof pending; not a pass"},
    {"audit_row": "P7", "name": "refmv-stack-check", "reason": "multi-reference/inter-syntax expansion pending; not a pass"},
    {"audit_row": "P8", "name": "ip-10f-last-public-check", "reason": "longer-GOP feature-lane implementation pending; not a pass"},
    {"audit_row": "P8", "name": "keyframe-interval-check", "reason": "session-control/reference-refresh coverage pending; not a pass"},
    {"audit_row": "P9", "name": "filters-disabled-public-check", "reason": "filter-scope public proof pending; not a pass"},
    {"audit_row": "P9", "name": "recon-ref-ownership-check", "reason": "reconstructed-reference ownership audit pending; not a pass"},
    {"audit_row": "P10", "name": "qindex-functional-sweep-check", "reason": "quality/rate-control feature-lane implementation pending; not a pass"},
    {"audit_row": "P10", "name": "mode-decision-stability-check", "reason": "mode-decision/search-quality harness coverage pending; not a pass"},
    {"audit_row": "P11", "name": "scale64-public-check", "reason": "scale-up public proof pending; not a pass"},
    {"audit_row": "P11", "name": "scale320-public-check", "reason": "scale-up public proof pending; not a pass"},
    {"audit_row": "P11", "name": "bbb720p24-10s-public-check", "reason": "final long run deferred until feature matrix is green; not a pass"},
    {"audit_row": "P11", "name": "mp4-packaging-check", "reason": "packaging ownership audit pending; not a pass"},
    {"audit_row": "P12", "name": "advanced-tool-scope-check", "reason": "deferred-tool scope/signaling audit pending; not a pass"},
    {"audit_row": "P13", "name": "lint-check", "reason": "ASIC-readiness lane pending after functional matrix; not a pass"},
    {"audit_row": "P13", "name": "synthesis-top-smoke", "reason": "ASIC-readiness synthesis flow pending after functional matrix; not a pass"},
]


def now_stamp() -> str:
    return _dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run_capture(cmd: Iterable[str], *, cwd: Path, env: dict[str, str], log_path: Path, timeout_seconds: int) -> tuple[int, float]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    argv = [str(x) for x in cmd]
    start = time.time()
    with log_path.open("w") as log:
        log.write(f"$ cd {cwd}\n")
        log.write("$ " + " ".join(shlex.quote(x) for x in argv) + "\n")
        log.write(f"$ timeout_seconds={timeout_seconds}\n")
        log.flush()
        try:
            proc = subprocess.run(argv, cwd=str(cwd), env=env, text=True, stdout=log, stderr=subprocess.STDOUT, timeout=timeout_seconds)
            return proc.returncode, time.time() - start
        except subprocess.TimeoutExpired:
            log.write(f"\n[TIMEOUT] gate exceeded {timeout_seconds} seconds\n")
            log.flush()
            return 124, time.time() - start


def capture_text(cmd: Iterable[str], *, cwd: Path = REPO) -> str:
    try:
        res = subprocess.run([str(x) for x in cmd], cwd=str(cwd), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        return (res.stdout or "").strip()
    except Exception as exc:
        return f"ERROR: {exc}"


def collect_hashes(root: Path) -> list[dict[str, object]]:
    if not root.exists():
        return []
    records = []
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        rel = path.relative_to(root)
        records.append({"path": str(rel), "size": path.stat().st_size, "sha256": sha256_file(path)})
    return records


def selected_gates(names: list[str] | None) -> list[Gate]:
    if not names:
        return CURRENT_GATES
    by_name = {g.name: g for g in CURRENT_GATES}
    by_id = {g.gate_id: g for g in CURRENT_GATES}
    out = []
    for name in names:
        gate = by_name.get(name) or by_id.get(name)
        if gate is None:
            raise SystemExit(f"unknown gate {name}; known: {', '.join(g.name for g in CURRENT_GATES)}")
        out.append(gate)
    return out


def write_markdown(path: Path, manifest: dict[str, object]) -> None:
    lines = []
    lines.append("# AV1 regression matrix manifest")
    lines.append("")
    lines.append(f"Repo: `{manifest['repo']}`")
    lines.append(f"Commit: `{manifest['commit']}`")
    lines.append(f"Generated: `{manifest['generated_at']}`")
    lines.append("")
    lines.append("## Toolchain")
    for key, value in manifest["toolchain"].items():
        lines.append(f"- {key}: `{value}`")
    lines.append("")
    lines.append("## Executed gates")
    lines.append("| Gate | Name | Status | Seconds | Log |")
    lines.append("|---|---|---:|---:|---|")
    for gate in manifest["gates"]:
        lines.append(f"| {gate['gate_id']} | {gate['name']} | {gate['status']} | {gate['elapsed_seconds']:.2f} | `{gate['log']}` |")
    lines.append("")
    lines.append("## Skipped future gates (not passes)")
    lines.append("| Audit row | Name | Reason |")
    lines.append("|---|---|---|")
    for gate in manifest["future_gates"]:
        lines.append(f"| {gate['audit_row']} | {gate['name']} | {gate['reason']} |")
    lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", type=Path, default=None, help="Manifest/artifact output dir")
    ap.add_argument("--gates", nargs="*", help="Optional gate ids/names to run instead of full current matrix")
    ap.add_argument("--keep-going", action="store_true", help="Continue after failures and return nonzero at end")
    ap.add_argument("--timeout-seconds", type=int, default=900, help="Per-gate timeout; timed-out gates return 124")
    args = ap.parse_args()

    gates = selected_gates(args.gates)
    outdir = args.outdir or (REPO / "regression_artifacts" / f"av1_matrix_{now_stamp()}")
    outdir = outdir.resolve()
    logs_dir = outdir / "logs"
    artifacts_dir = outdir / "artifacts"
    outdir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    base_env = os.environ.copy()
    base_env["THREADS"] = DEFAULT_THREADS
    base_env["BUILD_JOBS"] = DEFAULT_THREADS

    manifest: dict[str, object] = {
        "repo": str(REPO),
        "generated_at": _dt.datetime.now().isoformat(timespec="seconds"),
        "commit": capture_text(["git", "rev-parse", "HEAD"]),
        "branch": capture_text(["git", "branch", "--show-current"]),
        "git_status": capture_text(["git", "status", "--short", "--branch"]),
        "toolchain": {
            "host": capture_text(["hostname"]),
            "nproc": capture_text(["nproc"]),
            "verilator": capture_text(["verilator", "--version"]),
            "ffmpeg": capture_text(["ffmpeg", "-version"]).splitlines()[0] if capture_text(["ffmpeg", "-version"]) else "missing",
            "aomdec": capture_text(["aomdec", "--help"]).splitlines()[0] if capture_text(["aomdec", "--help"]) else "missing",
        },
        "gates": [],
        "future_gates": FUTURE_GATES,
    }

    failed = False
    run_namespace = _sanitize_tag(outdir.name)
    for gate in gates:
        print(f"[MATRIX] {gate.gate_id} {gate.name}")
        gate_artifacts = artifacts_dir / f"{gate.gate_id}_{gate.name}"
        env = gate_env(run_namespace, gate, base_env)
        env["AV1_ARTIFACT_ROOT"] = str(gate_artifacts)
        cmd = gate_command(run_namespace, gate)
        log_path = logs_dir / f"{gate.gate_id}_{gate.name}.log"
        rc, elapsed = run_capture(cmd, cwd=gate.cwd, env=env, log_path=log_path, timeout_seconds=args.timeout_seconds)
        status = "pass" if rc == 0 else "fail"
        if rc != 0:
            failed = True
        record = {
            "gate_id": gate.gate_id,
            "name": gate.name,
            "description": gate.description,
            "status": status,
            "returncode": rc,
            "elapsed_seconds": round(elapsed, 3),
            "command": " ".join(shlex.quote(x) for x in cmd),
            "cwd": str(gate.cwd),
            "log": str(log_path),
            "artifact_root": str(gate_artifacts),
            "artifact_hashes": collect_hashes(gate_artifacts),
            "public_decoder": gate.public_decoder,
            "av1_top_sim": env["AV1_TOP_SIM"],
            "build_namespace": gate_namespace(run_namespace, gate),
        }
        manifest["gates"].append(record)
        outdir.mkdir(parents=True, exist_ok=True)
        logs_dir.mkdir(parents=True, exist_ok=True)
        artifacts_dir.mkdir(parents=True, exist_ok=True)
        write_json_path = outdir / "av1_regression_manifest.json"
        write_json_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        write_markdown(outdir / "av1_regression_manifest.md", manifest)
        print(f"[MATRIX] {gate.name}: {status} ({elapsed:.2f}s) log={log_path}")
        if rc != 0 and not args.keep_going:
            break

    manifest["completed_at"] = _dt.datetime.now().isoformat(timespec="seconds")
    manifest["overall_status"] = "fail" if failed else "pass"
    outdir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    (outdir / "av1_regression_manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    write_markdown(outdir / "av1_regression_manifest.md", manifest)
    print(f"[MATRIX] manifest_json={outdir / 'av1_regression_manifest.json'}")
    print(f"[MATRIX] manifest_md={outdir / 'av1_regression_manifest.md'}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
