# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Codec-comparison mode (research-0061 Bucket #7).

Given a single source and a target VMAF, run the per-codec recommend
predicate in parallel and emit a ranked table of
``(codec, best_crf, bitrate_kbps, encode_time_ms, vmaf_score)`` tuples.

This module is intentionally a thin orchestration layer: the heavy
lifting lives in the per-codec recommend predicate, which is injected
via the ``predicate`` argument so tests stay subprocess-free and so
future codec adapters extend coverage without touching this file
(matches the codec-adapter discipline laid out in
``tools/vmaf-tune/AGENTS.md``).

The predicate signature is::

    predicate(codec: str, src: Path, target_vmaf: float) -> RecommendResult

The programmatic default predicate returns an ``ok=False`` row when
callers do not bind the source geometry required by Phase B bisect.
The CLI binds :func:`vmaftune.bisect.make_bisect_predicate` from its
``--width`` / ``--height`` / source-shape flags by default, and still
accepts a ``--predicate-module`` hook for custom backends or tests.
"""

from __future__ import annotations

import csv
import dataclasses
import io
import json
import math
import time
from collections.abc import Callable, Iterable, Sequence
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

from . import __version__ as TOOL_VERSION
from .codec_adapters import known_codecs

# Keys exposed to programmatic consumers. Mirrors the CORPUS_ROW_KEYS
# discipline in ``vmaftune/__init__.py``: bumping the list is a
# coordinated change.
COMPARE_ROW_KEYS: tuple[str, ...] = (
    "codec",
    "encoder_version",
    "best_crf",
    "bitrate_kbps",
    "encode_time_ms",
    "vmaf_score",
    "target_vmaf",
    "ok",
    "error",
)


@dataclasses.dataclass(frozen=True)
class RecommendResult:
    """One codec's best (CRF, bitrate, vmaf) tuple at a given target.

    ``ok=False`` carries a human-readable ``error`` string and leaves
    the numeric fields at sentinel values; the report renderer skips
    such rows in the ranking but still surfaces them in the table.
    """

    codec: str
    best_crf: int
    bitrate_kbps: float
    encode_time_ms: float
    vmaf_score: float
    encoder_version: str = ""
    ok: bool = True
    error: str = ""

    def to_row(self, target_vmaf: float) -> dict[str, Any]:
        return {
            "codec": self.codec,
            "encoder_version": self.encoder_version,
            "best_crf": self.best_crf,
            "bitrate_kbps": self.bitrate_kbps,
            "encode_time_ms": self.encode_time_ms,
            "vmaf_score": self.vmaf_score,
            "target_vmaf": target_vmaf,
            "ok": self.ok,
            "error": self.error,
        }


@dataclasses.dataclass(frozen=True)
class ComparisonReport:
    """Result of ``compare_codecs`` — sorted ranking + metadata.

    ``rows`` are ordered ascending by ``bitrate_kbps`` for ``ok=True``
    rows; failed rows trail the ranking sorted by codec name. The raw
    ordering matters because the renderers preserve it.
    """

    src: str
    target_vmaf: float
    tool_version: str
    wall_time_ms: float
    rows: tuple[RecommendResult, ...]

    def best(self) -> RecommendResult | None:
        """Return the smallest-bitrate ok row, or ``None`` if all failed."""
        for r in self.rows:
            if r.ok:
                return r
        return None


PredicateFn = Callable[[str, Path, float], RecommendResult]


def _default_predicate(codec: str, src: Path, target_vmaf: float) -> RecommendResult:
    """Default predicate — points callers at :func:`bisect.make_bisect_predicate`.

    Phase B (target-VMAF bisect, see ``vmaftune.bisect``) is the
    production backend, but the bisect needs source geometry
    (``width``, ``height``, ``pix_fmt``, ``framerate``,
    ``duration_s``) that the bare ``(codec, src, target_vmaf)``
    predicate signature does not carry. Operators bind those once via
    ``make_bisect_predicate(...)`` and pass the resulting closure
    into ``compare_codecs(predicate=...)`` (or, in the CLI, via
    ``--predicate-module MODULE:CALLABLE``).

    The default predicate therefore returns ``ok=False`` with a
    well-formed row so callers see a clean report instead of a crash;
    the error string names the bisect entry-point so the next person
    who reads it has a one-step fix. The "Phase B pending" wording
    used to live here pre-ADR-0322 — the bisect itself ships in this
    PR, but compare's default predicate stays a pointer because the
    geometry-binding rule ("geometry is fixed before the predicate
    is built") would otherwise force compare to grow new arguments
    just to forward them through.
    """
    return RecommendResult(
        codec=codec,
        best_crf=-1,
        bitrate_kbps=float("nan"),
        encode_time_ms=float("nan"),
        vmaf_score=float("nan"),
        ok=False,
        error=(
            "no predicate wired. Use vmaftune.bisect.make_bisect_predicate("
            "target_vmaf, width=..., height=..., framerate=..., duration_s=...) "
            "and pass the result via compare_codecs(predicate=...) or "
            "--predicate-module MODULE:CALLABLE."
        ),
    )


def _rank(rows: Iterable[RecommendResult]) -> tuple[RecommendResult, ...]:
    """Sort: ok rows by ascending bitrate, failed rows trailing by codec."""
    materialized = list(rows)
    ok_rows = sorted(
        (r for r in materialized if r.ok),
        key=lambda r: (r.bitrate_kbps, r.codec),
    )
    fail_rows = sorted(
        (r for r in materialized if not r.ok),
        key=lambda r: r.codec,
    )
    return tuple([*ok_rows, *fail_rows])


def compare_codecs(
    src: Path,
    target_vmaf: float,
    encoders: Sequence[str],
    *,
    predicate: PredicateFn | None = None,
    parallel: bool = True,
    max_workers: int | None = None,
) -> ComparisonReport:
    """Run ``predicate`` per codec and rank by smallest bitrate.

    ``parallel=True`` dispatches each codec to a thread pool — every
    predicate call already shells out to ffmpeg / vmaf, so the GIL
    is not the bottleneck. ``max_workers`` defaults to ``len(encoders)``.
    """
    if not encoders:
        raise ValueError("compare_codecs requires at least one encoder")
    pred = predicate if predicate is not None else _default_predicate
    src_path = Path(src)
    t0 = time.monotonic()

    results: list[RecommendResult] = []
    if parallel and len(encoders) > 1:
        workers = max_workers if max_workers is not None else len(encoders)
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(pred, codec, src_path, target_vmaf): codec for codec in encoders}
            for fut in as_completed(futures):
                codec = futures[fut]
                try:
                    results.append(fut.result())
                except Exception as exc:  # noqa: BLE001 — surface the error verbatim
                    results.append(
                        RecommendResult(
                            codec=codec,
                            best_crf=-1,
                            bitrate_kbps=float("nan"),
                            encode_time_ms=float("nan"),
                            vmaf_score=float("nan"),
                            ok=False,
                            error=f"{type(exc).__name__}: {exc}",
                        )
                    )
    else:
        for codec in encoders:
            try:
                results.append(pred(codec, src_path, target_vmaf))
            except Exception as exc:  # noqa: BLE001
                results.append(
                    RecommendResult(
                        codec=codec,
                        best_crf=-1,
                        bitrate_kbps=float("nan"),
                        encode_time_ms=float("nan"),
                        vmaf_score=float("nan"),
                        ok=False,
                        error=f"{type(exc).__name__}: {exc}",
                    )
                )

    return ComparisonReport(
        src=str(src_path),
        target_vmaf=float(target_vmaf),
        tool_version=TOOL_VERSION,
        wall_time_ms=(time.monotonic() - t0) * 1000.0,
        rows=_rank(results),
    )


def _emit_markdown(report: ComparisonReport) -> str:
    lines = [
        f"# Codec comparison — target VMAF {report.target_vmaf:g}",
        "",
        f"- Source: `{report.src}`",
        f"- Tool: `vmaf-tune {report.tool_version}`",
        f"- Wall time: {report.wall_time_ms:.1f} ms",
        "",
        "| Rank | Codec | Encoder | Best CRF | Bitrate (kbps) | "
        "Encode time (ms) | VMAF | Status |",
        "|---:|---|---|---:|---:|---:|---:|---|",
    ]
    rank = 0
    for r in report.rows:
        if r.ok:
            rank += 1
            rank_cell = str(rank)
            status = "ok"
        else:
            rank_cell = "—"
            status = f"fail: {r.error}"
        lines.append(
            f"| {rank_cell} | {r.codec} | {r.encoder_version or '—'} | "
            f"{r.best_crf if r.best_crf >= 0 else '—'} | "
            f"{r.bitrate_kbps:.1f} | {r.encode_time_ms:.1f} | "
            f"{r.vmaf_score:.2f} | {status} |"
        )
    best = report.best()
    if best is not None:
        lines.extend(
            [
                "",
                f"**Smallest file**: `{best.codec}` "
                f"at CRF {best.best_crf} → {best.bitrate_kbps:.1f} kbps "
                f"(VMAF {best.vmaf_score:.2f}).",
            ]
        )
    else:
        lines.extend(["", "**No codec succeeded.** See per-row error column."])
    return "\n".join(lines) + "\n"


def _nan_to_none(value: Any) -> Any:
    """Recursively substitute ``None`` for ``NaN`` / ``±inf`` floats.

    The compare-report JSON emitter previously relied on Python's
    default ``json.dumps`` (``allow_nan=True``), which writes bare
    ``NaN`` / ``Infinity`` tokens. Those tokens are valid only under a
    JavaScript-extended grammar — RFC 8259 strict parsers (Go's
    ``encoding/json``, Rust ``serde_json``, ``jq --strict``,
    ``json.loads(..., parse_constant=...)``) reject them. The fix is to
    coerce non-finite floats to ``null`` before serialization so the
    output is portable; downstream readers that need to distinguish a
    failed row from a missing key can rely on the row-level ``ok`` flag
    + ``error`` string (Bug #2, BBB e2e 2026-05-17).
    """
    if isinstance(value, float):
        return None if math.isnan(value) or math.isinf(value) else value
    if isinstance(value, dict):
        return {k: _nan_to_none(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_nan_to_none(v) for v in value]
    return value


def _emit_json(report: ComparisonReport) -> str:
    payload = {
        "src": report.src,
        "target_vmaf": report.target_vmaf,
        "tool_version": report.tool_version,
        "wall_time_ms": report.wall_time_ms,
        "rows": [r.to_row(report.target_vmaf) for r in report.rows],
    }
    # ``allow_nan=False`` is a defence in depth — together with
    # ``_nan_to_none`` it guarantees the emitter never writes bare
    # ``NaN`` (which is not valid RFC 8259 JSON).
    return json.dumps(_nan_to_none(payload), indent=2, sort_keys=True, allow_nan=False) + "\n"


def _emit_csv(report: ComparisonReport) -> str:
    buf = io.StringIO()
    writer = csv.DictWriter(buf, fieldnames=list(COMPARE_ROW_KEYS))
    writer.writeheader()
    for r in report.rows:
        writer.writerow(r.to_row(report.target_vmaf))
    return buf.getvalue()


_EMITTERS: dict[str, Callable[[ComparisonReport], str]] = {
    "markdown": _emit_markdown,
    "json": _emit_json,
    "csv": _emit_csv,
}


def emit_report(report: ComparisonReport, format: str = "markdown") -> str:
    """Render ``report`` as ``markdown`` / ``json`` / ``csv``."""
    fmt = format.lower()
    if fmt not in _EMITTERS:
        raise ValueError(f"unknown format {format!r}; expected one of {sorted(_EMITTERS)}")
    return _EMITTERS[fmt](report)


def supported_formats() -> tuple[str, ...]:
    return tuple(sorted(_EMITTERS))


# The canonical default codec list for the CLI. We only advertise codecs
# whose adapters actually live in ``codec_adapters/`` — adding x265 /
# SVT-AV1 / libaom / libvvenc later auto-expands this set.
def default_encoders() -> tuple[str, ...]:
    return known_codecs()


__all__ = [
    "COMPARE_ROW_KEYS",
    "ComparisonReport",
    "PredicateFn",
    "RecommendResult",
    "compare_codecs",
    "default_encoders",
    "emit_report",
    "supported_formats",
]
