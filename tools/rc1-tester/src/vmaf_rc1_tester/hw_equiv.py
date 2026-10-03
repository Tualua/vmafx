# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Score-level checks of the tester image: dispatch and reference equivalence.

Every CPU feature extractor runs on each fixture at `--precision max` (`%.17g`),
once with the default dispatch and once with every SIMD flag masked off
(`--cpumask`, scalar C). Scores are compared with `==`, never a tolerance.
"""

from __future__ import annotations

import json
import tempfile
from collections.abc import Callable, Mapping, Sequence
from pathlib import Path
from typing import Any

from .safe_process import run_bounded

# Every extractor registered on the CPU backend that runs on a plain YUV pair.
EXTRACTORS = (
    "float_psnr", "float_adm", "float_vif", "float_motion", "float_moment",
    "speed_chroma", "speed_temporal", "float_ms_ssim", "float_ssim", "ssim",
    "ssimulacra2", "y_funque_plus", "niqe", "brisque", "ciede", "delta_e_itp",
    "pu21", "psnr", "psnr_hvs", "adm", "motion", "motion_v2", "vif", "cambi",
)  # fmt: skip
SCALAR_CPUMASK = 0xFFFFFFFF
MAX_REPORTED_METRICS = 40
MAX_FRAMES = 100_000
# A metric the extractor could not finite-evaluate is JSON null; null == null.
Scores = dict[str, list[float | None]]
Runner = Callable[..., Any]


class FixtureRunError(RuntimeError):
    """The vmaf binary failed or produced unusable output for a fixture."""

    def __init__(self, message: str, returncode: int | None = None) -> None:
        super().__init__(message)
        self.returncode = returncode


def build_argv(
    vmaf: str,
    fixture: Mapping[str, Any],
    out_json: str,
    cpumask: int | None,
    backend: str = "cpu",
) -> list[str]:
    """Command line for one fixture; `cpumask` None selects the default dispatch."""
    argv = [
        vmaf, "--reference", str(fixture["ref"]), "--distorted", str(fixture["dis"]),
        "--width", str(fixture["width"]), "--height", str(fixture["height"]),
        "--pixel_format", str(fixture["pixel_format"]), "--bitdepth", str(fixture["bitdepth"]),
        "--json", "--output", out_json, "--precision", "max", "--threads", "1",
        "--backend", backend, "--quiet",
    ]  # fmt: skip
    for name in EXTRACTORS:
        argv += ["--feature", name]
    if cpumask is not None:
        argv += ["--cpumask", str(cpumask)]
    return argv


def parse_scores(text: str) -> Scores:
    """Per-metric frame series from a vmaf JSON report."""
    try:
        frames = json.loads(text)["frames"]
        scores: Scores = {}
        for index, frame in enumerate(frames[:MAX_FRAMES]):
            if int(frame["frameNum"]) != index:
                raise FixtureRunError("frame numbers are not consecutive")
            for name, value in frame["metrics"].items():
                scores.setdefault(name, []).append(None if value is None else float(value))
    except (KeyError, TypeError, ValueError) as error:
        raise FixtureRunError(f"unreadable vmaf JSON: {error}") from error
    if not scores:
        raise FixtureRunError("vmaf JSON holds no frames")
    return scores


def run_fixture_meta(
    vmaf: str,
    fixture: Mapping[str, Any],
    cpumask: int | None,
    *,
    timeout_seconds: float,
    runner: Runner = run_bounded,
    backend: str = "cpu",
) -> tuple[Scores, dict[str, Any]]:
    """Run vmaf on one fixture; returns its scores and the JSON's backend facts."""
    with tempfile.TemporaryDirectory(prefix="vmaf-tester-") as work:
        out_json = str(Path(work) / "out.json")
        result = runner(
            build_argv(vmaf, fixture, out_json, cpumask, backend),
            timeout_seconds=timeout_seconds,
            max_output_bytes=1_048_576,
        )
        if result.returncode != 0:
            tail = result.stderr.strip().splitlines()[-1:] or ["no output"]
            raise FixtureRunError(f"vmaf exited {result.returncode}: {tail[0]}", result.returncode)
        try:
            text = Path(out_json).read_text(encoding="utf-8")
        except OSError as error:
            raise FixtureRunError(f"no output file: {error}") from error
    document = json.loads(text)
    meta = {
        "backend_used": document.get("backend_used"),
        "feature_backends": document.get("feature_backends", []),
    }
    return parse_scores(text), meta


def run_fixture(
    vmaf: str,
    fixture: Mapping[str, Any],
    cpumask: int | None,
    *,
    timeout_seconds: float,
    runner: Runner = run_bounded,
) -> Scores:
    """Run vmaf on one fixture and return its scores."""
    return run_fixture_meta(vmaf, fixture, cpumask, timeout_seconds=timeout_seconds, runner=runner)[
        0
    ]


def _fmt(value: float | None) -> str:
    return "null" if value is None else f"{value:.17g}"


def _max_abs_diff(a: Sequence[float | None], b: Sequence[float | None]) -> float:
    """Largest |a - b| over the frames where both are numbers."""
    pairs = [(x, y) for x, y in zip(a, b, strict=True) if x is not None and y is not None]
    return max((abs(x - y) for x, y in pairs), default=0.0)


def _compare_series(
    name: str, a: Sequence[float | None] | None, b: Sequence[float | None] | None
) -> tuple[int, dict[str, Any] | None]:
    """(number of values compared, detail of the difference or None)."""
    if a is None or b is None or len(a) != len(b):
        count = max(len(a or []), len(b or []))
        detail = {
            "metric": name,
            "differing_values": count,
            "first_frame": 0,
            "left": "absent" if a is None else f"{len(a)} frames",
            "right": "absent" if b is None else f"{len(b)} frames",
        }
        return count, detail
    bad = [index for index in range(len(a)) if a[index] != b[index]]
    if not bad:
        return 0, None
    first = bad[0]
    detail = {
        "metric": name,
        "differing_values": len(bad),
        "first_frame": first,
        "left": _fmt(a[first]),
        "right": _fmt(b[first]),
        "max_abs_diff": _fmt(_max_abs_diff(a, b)),
    }
    return len(bad), detail


def compare_scores(left: Scores, right: Scores) -> dict[str, Any]:
    """Exact comparison of two score sets; differing values are kept at `%.17g`."""
    names = sorted(set(left) | set(right))
    details: list[dict[str, Any]] = []
    total = sum(max(len(left.get(n, ())), len(right.get(n, ()))) for n in names)
    differing = 0
    for name in names:
        count, detail = _compare_series(name, left.get(name), right.get(name))
        differing += count
        if detail is not None:
            details.append(detail)
    return {
        "metrics": len(names),
        "values": total,
        "differing_values": differing,
        "differing_metrics": len(details),
        "details": details[:MAX_REPORTED_METRICS],
        "details_truncated": max(0, len(details) - MAX_REPORTED_METRICS),
    }


def status_of(cells: Sequence[Mapping[str, Any]]) -> str:
    """`identical`, `differing` or `error` over a list of fixture cells."""
    if any("error" in cell for cell in cells):
        return "error"
    return "differing" if any(c["differing_values"] for c in cells) else "identical"


def run_dispatch_equivalence(
    vmaf: str,
    fixtures: Sequence[Mapping[str, Any]],
    *,
    timeout_seconds: float,
    runner: Runner = run_bounded,
) -> tuple[dict[str, Any], dict[str, tuple[Scores, Scores]]]:
    """Default dispatch against scalar on every fixture; also returns the raw scores."""
    cells: list[dict[str, Any]] = []
    raw: dict[str, tuple[Scores, Scores]] = {}
    for fixture in fixtures:
        cell: dict[str, Any] = {"fixture": fixture["id"]}
        try:
            default = run_fixture(
                vmaf, fixture, None, timeout_seconds=timeout_seconds, runner=runner
            )
            scalar = run_fixture(
                vmaf, fixture, SCALAR_CPUMASK, timeout_seconds=timeout_seconds, runner=runner
            )
        except (FixtureRunError, TimeoutError, RuntimeError, ValueError) as error:
            cell["error"] = str(error)
        else:
            raw[str(fixture["id"])] = (default, scalar)
            cell.update(compare_scores(default, scalar))
        cells.append(cell)
    return {"status": status_of(cells), "fixtures": cells}, raw
