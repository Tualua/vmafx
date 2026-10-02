#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Stage-aware verdicts for the libvmaf_sycl zero-copy end-to-end harness.

``scripts/test/zerocopy-e2e.sh`` runs every case through three legs on the same
QSV-encoded pair and writes ``<clip>_<depth>bit__<case>.<leg>.{json,rc,err}``:

* ``cpu``  - software decode into ``libvmaf`` (the reference);
* ``host`` - software decode into ``libvmaf_sycl`` (host upload, SYCL twins);
* ``zc``   - QSV decode into ``libvmaf_sycl`` (zero-copy).

This module reads those files and decides, per case:

* ``host`` must equal ``cpu`` exactly (D-04); otherwise ``NONEXACT host-vs-cpu``.
  The tolerance ``resolve_cell_tolerance()`` would grant is printed for
  information only and never applied.
* A case whose stage (``PARITY_STAGE``) is at or below ``--stage`` must succeed
  on ``zc`` with every CPU metric present (``FAIL silent-drop`` otherwise) and
  equal to ``host`` (``FAIL zc-vs-host`` otherwise).
* A case of a later stage must fail loudly: non-zero exit and stderr naming the
  CPU feature or its SYCL twin. A success is ``FAIL unexpected-success`` and a
  failure that names nothing is ``FAIL unnamed-failure``. The comparator never
  matches message text beyond the feature names, so the wording may change
  between stages.

An absent metric in a successful zero-copy output is never a pass (T-12-07).

Cases dropped from the plan's matrix: ``motion`` with ``motion_add_uv=true``.
The CPU ``motion`` extractor has no such option (only ``float_motion`` does), so
the CPU leg cannot run it. ``float_motion_uv`` covers the option on the float
extractor instead, at stage 3 where ``float_motion`` is migrated.

Usage::

    zerocopy_e2e_compare.py --stage {1,2,3} --dir DIR [--cases a,b,...]
    zerocopy_e2e_compare.py --list [--cases a,b,...]
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.ci.cross_backend_parity_gate import (
    diff_frames,
    load_frames,
    missing_metrics,
    resolve_cell_tolerance,
)

LEGS = ("cpu", "host", "zc")


@dataclass(frozen=True)
class Case:
    """One matrix row.

    ``kind`` is ``feature`` or ``model``; ``arg`` is the value of the filter's
    ``feature=`` / ``model=`` option (options after the name escaped with ``\\:``
    as docs/usage/ffmpeg.md shows); ``names`` are the strings a loud failure may
    use to name the case (CPU feature name or SYCL twin name); ``tolerance_key``
    is the feature handed to ``resolve_cell_tolerance`` for the info line.
    """

    kind: str
    arg: str
    names: tuple[str, ...]
    tolerance_key: str


def _feature(name: str, opts: str = "", *aliases: str) -> Case:
    arg = f"name={name}" + (f"\\:{opts}" if opts else "")
    return Case("feature", arg, (name, f"{name}_sycl", *aliases), name)


CASES: dict[str, Case] = {
    # Stage 1: luma-only features and the integer model are zero-copy correct.
    "vif": _feature("vif"),
    "adm": _feature("adm"),
    "motion": _feature("motion"),
    "motion_v2": _feature("motion_v2"),
    "cambi": _feature("cambi"),
    "float_moment": _feature("float_moment"),
    "psnr_luma": _feature("psnr", "enable_chroma=false"),
    "psnr_hvs_luma": _feature("psnr_hvs", "enable_chroma=false"),
    "model-vmaf_v0.6.1": Case("model", "version=vmaf_v0.6.1", ("vmaf_v0.6.1",), "vif"),
    # Stage 2: chroma import.
    "psnr": _feature("psnr"),
    "psnr_hvs": _feature("psnr_hvs"),
    # Stage 3: host-staging extractors migrated to the shared planes.
    "float_ssim": _feature("float_ssim"),
    "float_ms_ssim": _feature("float_ms_ssim"),
    "float_psnr": _feature("float_psnr"),
    "float_adm": _feature("float_adm"),
    "float_vif": _feature("float_vif"),
    "float_motion": _feature("float_motion"),
    "float_motion_uv": _feature("float_motion", "motion_add_uv=true"),
    "ssim": _feature("ssim", "", "integer_ssim"),
    "ciede": _feature("ciede"),
    "ssimulacra2": _feature("ssimulacra2"),
    "speed_chroma": _feature("speed_chroma"),
    "speed_temporal": _feature("speed_temporal"),
    "model-vmaf_float_v0.6.1": Case(
        "model",
        "version=vmaf_float_v0.6.1",
        ("vmaf_float_v0.6.1", "float_adm", "float_vif", "float_motion"),
        "float_adm",
    ),
}

# Stage from which zero-copy must match the CPU numerically.
PARITY_STAGE: dict[str, int] = {
    "vif": 1,
    "adm": 1,
    "motion": 1,
    "motion_v2": 1,
    "cambi": 1,
    "float_moment": 1,
    "psnr_luma": 1,
    "psnr_hvs_luma": 1,
    "model-vmaf_v0.6.1": 1,
    "psnr": 2,
    "psnr_hvs": 2,
    "float_ssim": 3,
    "float_ms_ssim": 3,
    "float_psnr": 3,
    "float_adm": 3,
    "float_vif": 3,
    "float_motion": 3,
    "float_motion_uv": 3,
    "ssim": 3,
    "ciede": 3,
    "ssimulacra2": 3,
    "speed_chroma": 3,
    "speed_temporal": 3,
    "model-vmaf_float_v0.6.1": 3,
}


@dataclass(frozen=True)
class Verdict:
    """``kind`` is PASS, FAIL or NONEXACT; ``label`` names the reason."""

    kind: str
    label: str
    detail: str = ""


def _leg_file(base: Path, ext: str) -> Path:
    """``<prefix>.<leg>`` plus ``ext``; ``with_suffix`` would eat dotted case ids."""

    return base.with_name(base.name + ext)


def _read_rc(base: Path) -> int | None:
    try:
        return int(_leg_file(base, ".rc").read_text().strip())
    except (OSError, ValueError):
        return None


def _read_err(base: Path) -> str:
    try:
        return _leg_file(base, ".err").read_text(errors="replace")
    except OSError:
        return ""


def _frames(base: Path) -> list[dict[str, Any]] | None:
    try:
        return load_frames(_leg_file(base, ".json"))
    except (OSError, ValueError):
        return None


def _metric_keys(frames: list[dict[str, Any]]) -> tuple[str, ...]:
    keys: set[str] = set()
    for frame in frames:
        keys.update(frame.get("metrics", {}))
    return tuple(sorted(keys))


def _worst(per_max: dict[str, float], per_mismatch: dict[str, int]) -> str:
    bad = sorted(((d, m) for m, d in per_max.items() if per_mismatch[m] or d > 0), reverse=True)
    return ", ".join(f"{m} max={d:.3e}" for d, m in bad[:5])


def _compare_exact(
    a: list[dict[str, Any]], b: list[dict[str, Any]], keys: tuple[str, ...]
) -> str | None:
    """None when ``a`` and ``b`` agree exactly on ``keys``, else a description."""

    if len(a) != len(b):
        return f"frame count {len(a)} vs {len(b)}"
    gone = missing_metrics(b, keys)
    if gone:
        return "missing " + ",".join(gone)
    per_max, per_mismatch = diff_frames(a, b, keys, 0.0)
    if any(per_mismatch.values()):
        return _worst(per_max, per_mismatch)
    return None


def _host_vs_cpu(case: Case, cpu: list[dict[str, Any]], host: list[dict[str, Any]]) -> str | None:
    problem = _compare_exact(cpu, host, _metric_keys(cpu))
    if problem is None:
        return None
    tol, source = resolve_cell_tolerance(
        case.tolerance_key,
        fp16_features=(),
        calibration=None,
        gpu_id=None,
        backends=("cpu", "sycl"),
    )
    return f"{problem} tolerance(info)={tol:g} ({source}); applied 0"


def _names_failure(case: Case, err: str) -> bool:
    return any(n in err for n in case.names)


def _loud_fail_verdict(case: Case, zc_rc: int, zc_err: str) -> Verdict:
    if zc_rc == 0:
        return Verdict("FAIL", "unexpected-success", "zero-copy succeeded before its stage")
    if not _names_failure(case, zc_err):
        tail = zc_err.strip().splitlines()[-1:] or ["<empty stderr>"]
        return Verdict("FAIL", "unnamed-failure", f"stderr names none of {case.names}: {tail[0]}")
    return Verdict("PASS", "loud-fail", f"rc={zc_rc}")


def _parity_verdict(
    zc_rc: int,
    zc_err: str,
    base: Path,
    cpu: list[dict[str, Any]],
    host: list[dict[str, Any]],
) -> Verdict:
    if zc_rc != 0:
        tail = zc_err.strip().splitlines()[-1:] or ["<empty stderr>"]
        return Verdict("FAIL", "zc-failed", f"rc={zc_rc}: {tail[0]}")
    zc = _frames(base)
    if zc is None:
        return Verdict("FAIL", "missing-leg", "zc output JSON unreadable")
    if len(zc) != len(host):
        return Verdict("FAIL", "zc-vs-host", f"frame count {len(host)} vs {len(zc)}")
    gone = missing_metrics(zc, _metric_keys(cpu))
    if gone:
        return Verdict("FAIL", "silent-drop", "absent from zero-copy output: " + ",".join(gone))
    problem = _compare_exact(host, zc, _metric_keys(cpu))
    if problem is not None:
        return Verdict("FAIL", "zc-vs-host", problem)
    return Verdict("PASS", "", f"frames={len(zc)} metrics={len(_metric_keys(cpu))}")


def _reference_legs(
    d: Path, prefix: str
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]] | Verdict:
    """Frames of the CPU and host legs, or the verdict explaining why not."""

    out: list[list[dict[str, Any]]] = []
    for leg in ("cpu", "host"):
        base = d / f"{prefix}.{leg}"
        rc = _read_rc(base)
        if rc is None:
            return Verdict("FAIL", "missing-leg", f"{leg} rc file absent")
        if rc != 0:
            return Verdict("FAIL", f"{leg}-failed", f"rc={rc}")
        frames = _frames(base)
        if frames is None:
            return Verdict("FAIL", "missing-leg", f"{leg} output JSON unreadable")
        out.append(frames)
    return out[0], out[1]


def evaluate_case(d: Path, prefix: str, case_id: str, stage: int) -> Verdict:
    """Verdict for one case. ``prefix`` is ``<clip>_<depth>bit__<case>``."""

    case = CASES[case_id]
    refs = _reference_legs(d, prefix)
    if isinstance(refs, Verdict):
        return refs
    cpu, host = refs
    zc_base = d / f"{prefix}.zc"
    zc_rc = _read_rc(zc_base)
    if zc_rc is None:
        return Verdict("FAIL", "missing-leg", "zc rc file absent")
    zc_err = _read_err(zc_base)
    if PARITY_STAGE[case_id] > stage:
        verdict = _loud_fail_verdict(case, zc_rc, zc_err)
    else:
        verdict = _parity_verdict(zc_rc, zc_err, zc_base, cpu, host)
    if verdict.kind == "FAIL":
        return verdict
    nonexact = _host_vs_cpu(case, cpu, host)
    if nonexact is not None:
        return Verdict("NONEXACT", "host-vs-cpu", nonexact)
    return verdict


def _discover(d: Path) -> list[tuple[str, str]]:
    """``(clip, depth)`` pairs that have at least one case file in ``d``."""

    found: set[tuple[str, str]] = set()
    for rc_file in d.glob("*__*.cpu.rc"):
        head = rc_file.name.split("__", 1)[0]
        clip, _, depth = head.rpartition("_")
        found.add((clip, depth.removesuffix("bit")))
    return sorted(found)


def _parse_cases(raw: str | None) -> list[str]:
    if not raw:
        return list(PARITY_STAGE)
    ids = [c for c in raw.split(",") if c]
    unknown = [c for c in ids if c not in PARITY_STAGE]
    if unknown:
        raise SystemExit(f"unknown case id(s): {', '.join(unknown)}")
    return ids


def _format(clip: str, depth: str, case_id: str, v: Verdict) -> str:
    label = f" {v.label}" if v.label else ""
    detail = f" {v.detail}" if v.detail else ""
    return f"ZC-E2E {clip} {depth} {case_id} {v.kind}{label}{detail}"


def _run(args: argparse.Namespace) -> int:
    d = Path(args.dir)
    cases = _parse_cases(args.cases)
    counts = {"PASS": 0, "FAIL": 0, "NONEXACT": 0}
    for clip, depth in _discover(d):
        for case_id in cases:
            prefix = f"{clip}_{depth}bit__{case_id}"
            verdict = evaluate_case(d, prefix, case_id, args.stage)
            counts[verdict.kind] += 1
            print(_format(clip, depth, case_id, verdict))
    print(
        f"ZC-E2E SUMMARY stage={args.stage} pass={counts['PASS']} "
        f"fail={counts['FAIL']} nonexact={counts['NONEXACT']}"
    )
    ok = counts["PASS"] > 0 and counts["FAIL"] == 0 and counts["NONEXACT"] == 0
    return 0 if ok else 1


def _list_cases(raw: str | None) -> int:
    for case_id in _parse_cases(raw):
        case = CASES[case_id]
        print(f"{case_id}\t{case.kind}\t{case.arg}\t{PARITY_STAGE[case_id]}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--stage", type=int, choices=(1, 2, 3))
    ap.add_argument("--dir")
    ap.add_argument("--cases", help="comma-separated case ids (default: all)")
    ap.add_argument("--list", action="store_true", help="print id, kind, filter arg, stage")
    args = ap.parse_args(argv)
    if args.list:
        return _list_cases(args.cases)
    if args.stage is None or not args.dir:
        ap.error("--stage and --dir are required")
    return _run(args)


if __name__ == "__main__":
    sys.exit(main())
