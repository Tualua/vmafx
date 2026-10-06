#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Parse, summarise and compare libvmaf_sycl zero-copy throughput runs.

``scripts/test/zerocopy-throughput.sh`` records, per run, ``BASE.{err,json,time,rc,
fdinfo.first,fdinfo.last,freq}`` and calls ``parse-run`` once to append one JSON
line to ``results.jsonl``.

Subcommands::

    parse-run --base B --meta k=v [k=v ...]
    summarize --results F [--max-spread 0.05]
    same-scores A.json B.json

``same-scores`` reuses the exact comparison of ``zerocopy_e2e_compare`` (HISS-19).
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from zerocopy_e2e_compare import _compare_exact, _metric_keys, load_frames

RTIME_RE = re.compile(r"rtime=([0-9.]+)s")
TIMING_RE = re.compile(
    r"\[vmaf-sycl\] timing: (\d+) frames, avg cpu=([0-9.]+)ms gpu=([0-9.]+)ms total=([0-9.]+)ms"
)
PHASES_RE = re.compile(r"^\[vmaf-sycl\] phases:(.*)$", re.M)
PHASE_PAIR_RE = re.compile(r"(\w+)=([0-9.]+)ms")
TIME_RE = re.compile(r"TIME real=([0-9.]+) user=([0-9.]+) sys=([0-9.]+)")
ENGINE_RE = re.compile(r"^(drm-engine-[\w-]+):\s*(\d+)\s*ns", re.M)
GROUP_KEYS = ("label", "ladder", "model", "feature", "n_subsample", "env")
EXIT_FAILED_RUN = 1
EXIT_INVALID_SPREAD = 3
MS_PER_S = 1000.0
MIN_REPEATS = 2


def _read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def _engines(path: Path) -> dict[str, int]:
    """Engine busy ns summed over every DRM client (fd) of the process."""

    totals: dict[str, int] = {}
    for key, val in ENGINE_RE.findall(_read(path)):
        totals[key] = totals.get(key, 0) + int(val)
    return totals


def _frame_count(base: Path, meta: dict[str, str]) -> tuple[int, dict[str, Any]]:
    """Frames of the run and its parsed JSON log ({} when there is none)."""

    text = _read(Path(f"{base}.json"))
    if text:
        try:
            doc = json.loads(text)
        except json.JSONDecodeError:
            doc = {}
        if isinstance(doc, dict) and isinstance(doc.get("frames"), list):
            return len(doc["frames"]), doc
    return int(meta.get("frames", "0") or 0), {}


def _parse_err(err: str) -> dict[str, Any]:
    out: dict[str, Any] = {}
    rtimes = RTIME_RE.findall(err)
    if rtimes:
        out["rtime_s"] = float(rtimes[-1])
    timing = TIMING_RE.findall(err)
    if timing:
        n, cpu, gpu, total = timing[-1]
        out.update(
            sycl_timing_frames=int(n),
            sycl_avg_cpu_ms=float(cpu),
            sycl_avg_gpu_ms=float(gpu),
            sycl_avg_total_ms=float(total),
        )
    phases = PHASES_RE.findall(err)
    if phases:
        out["phases"] = {k: float(v) for k, v in PHASE_PAIR_RE.findall(phases[-1])}
    return out


def _parse_time(text: str, frames: int) -> dict[str, Any]:
    m = TIME_RE.search(text)
    if not m:
        return {}
    real, user, sys_s = (float(x) for x in m.groups())
    out: dict[str, Any] = {"real_s": real, "host_user_s": user, "host_sys_s": sys_s}
    if frames > 0:
        out["host_cpu_ms_per_frame"] = (user + sys_s) * MS_PER_S / frames
    return out


def _parse_engines(base: Path, span_s: float | None) -> dict[str, Any]:
    first, last = _engines(Path(f"{base}.fdinfo.first")), _engines(Path(f"{base}.fdinfo.last"))
    if not first or not last:
        return {}
    delta = {k: last[k] - first[k] for k in last if k in first}
    out: dict[str, Any] = {"engine_ns": delta}
    if span_s and span_s > 0:
        out["engine_busy_pct"] = {k: 100.0 * v / (span_s * 1e9) for k, v in delta.items()}
    return out


def _parse_freq(path: Path) -> dict[str, float]:
    vals = []
    for tok in _read(path).split():
        try:
            vals.append(float(tok))
        except ValueError:
            continue
    if not vals:
        return {}
    return {"min": min(vals), "median": statistics.median(vals), "max": max(vals)}


def _pooled_mean(doc: dict[str, Any]) -> float | None:
    pooled = doc.get("pooled_metrics", {})
    vmaf = pooled.get("vmaf") if isinstance(pooled, dict) else None
    mean = vmaf.get("mean") if isinstance(vmaf, dict) else None
    return float(mean) if isinstance(mean, (int, float)) else None


def parse_run(base: Path, meta: dict[str, str]) -> dict[str, Any]:
    """One result row for the run recorded at ``base``."""

    frames, doc = _frame_count(base, meta)
    row: dict[str, Any] = dict(meta)
    row["frames"] = frames
    err = _parse_err(_read(Path(f"{base}.err")))
    row.update(err)
    if err.get("rtime_s") and frames > 0:
        row["fps"] = frames / err["rtime_s"]
    timing = _parse_time(_read(Path(f"{base}.time")), frames)
    row.update(timing)
    row.update(_parse_engines(base, timing.get("real_s")))
    freq = _parse_freq(Path(f"{base}.freq"))
    if freq:
        row["freq_mhz"] = freq
    mean = _pooled_mean(doc)
    if mean is not None:
        row["vmaf_mean"] = mean
    rc_text = _read(Path(f"{base}.rc")).strip()
    row["rc"] = int(rc_text) if rc_text.lstrip("-").isdigit() else -1
    return row


def _group_of(row: dict[str, Any]) -> tuple[str, ...]:
    return tuple(str(row.get(k, "")) for k in GROUP_KEYS)


def _median(rows: list[dict[str, Any]], key: str) -> float | None:
    vals = [r[key] for r in rows if isinstance(r.get(key), (int, float))]
    return statistics.median(vals) if vals else None


def _fmt(v: float | None, spec: str = ".2f") -> str:
    return "-" if v is None else format(v, spec)


def _engine_median(rows: list[dict[str, Any]]) -> str:
    keys = sorted({k for r in rows for k in r.get("engine_busy_pct", {})})
    parts = []
    for k in keys:
        vals = [r["engine_busy_pct"][k] for r in rows if k in r.get("engine_busy_pct", {})]
        parts.append(f"{k.removeprefix('drm-engine-')}={statistics.median(vals):.1f}")
    return " ".join(parts) or "-"


def _load_rows(path: Path) -> list[dict[str, Any]]:
    rows = []
    for line in _read(path).splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def _spread_problem(name: str, rows: list[dict[str, Any]], max_spread: float) -> int:
    if any(r.get("rc", 0) != 0 for r in rows):
        print(f"FAILED-RUN {name}")
        return EXIT_FAILED_RUN
    fps = [r["fps"] for r in rows if isinstance(r.get("fps"), (int, float))]
    if len(fps) >= MIN_REPEATS and statistics.median(fps) > 0:
        if (max(fps) - min(fps)) / statistics.median(fps) > max_spread:
            print(f"INVALID-SPREAD {name}")
            return EXIT_INVALID_SPREAD
    return 0


def summarize(rows: list[dict[str, Any]], max_spread: float) -> int:
    """Print the Markdown table; return the worst exit code over all groups."""

    groups: dict[tuple[str, ...], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        groups[_group_of(row)].append(row)
    print(
        "| group | n | fps med | fps min | fps max | sycl gpu ms | host cpu ms/frame | engines % |"
    )
    print("|---|---|---|---|---|---|---|---|")
    worst = 0
    for key in sorted(groups):
        grp, name = groups[key], "/".join(key)
        fps = [r["fps"] for r in grp if isinstance(r.get("fps"), (int, float))]
        print(
            f"| {name} | {len(grp)} | {_fmt(_median(grp, 'fps'))} | {_fmt(min(fps, default=None))} "
            f"| {_fmt(max(fps, default=None))} | {_fmt(_median(grp, 'sycl_avg_gpu_ms'))} "
            f"| {_fmt(_median(grp, 'host_cpu_ms_per_frame'))} | {_engine_median(grp)} |"
        )
        code = _spread_problem(name, grp, max_spread)
        if code and (worst == 0 or code == EXIT_FAILED_RUN):
            worst = code
    return worst


def same_scores(a: Path, b: Path) -> int:
    """0 when two libvmaf JSON logs agree exactly on every metric and pooled value."""

    fa, fb = load_frames(a), load_frames(b)
    problem = _compare_exact(fa, fb, _metric_keys(fa))
    if problem is None:
        pa = json.loads(a.read_text()).get("pooled_metrics", {})
        pb = json.loads(b.read_text()).get("pooled_metrics", {})
        if pa != pb:
            problem = "pooled_metrics differ"
    if problem is not None:
        print(f"DIFF {problem}")
        return 1
    print(f"IDENTICAL frames={len(fa)}")
    return 0


def _meta_dict(items: list[str]) -> dict[str, str]:
    meta = {}
    for item in items:
        key, sep, val = item.partition("=")
        if not sep:
            raise SystemExit(f"bad --meta item (want k=v): {item}")
        meta[key] = val
    return meta


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("parse-run")
    p.add_argument("--base", required=True)
    p.add_argument("--meta", nargs="*", default=[])
    s = sub.add_parser("summarize")
    s.add_argument("--results", required=True)
    s.add_argument("--max-spread", type=float, default=0.05)
    c = sub.add_parser("same-scores")
    c.add_argument("a")
    c.add_argument("b")
    args = ap.parse_args(argv)
    if args.cmd == "parse-run":
        base = Path(args.base)
        row = parse_run(base, _meta_dict(args.meta))
        with (base.parent / "results.jsonl").open("a") as f:
            f.write(json.dumps(row, sort_keys=True) + "\n")
        return 0
    if args.cmd == "summarize":
        return summarize(_load_rows(Path(args.results)), args.max_spread)
    return same_scores(Path(args.a), Path(args.b))


if __name__ == "__main__":
    sys.exit(main())
