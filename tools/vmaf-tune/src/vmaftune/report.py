# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Profile-card report renderer for a tuned video.

Takes the structured outputs of the vmaf-tune pipeline (ladder build,
codec comparison, per-shot tune) and emits a single self-contained
artifact in either Markdown (with inline PNGs) or HTML (with inline
SVGs) form. Both formats share one ``ReportData`` dataclass and one
template loader, so adding a third (e.g. PDF via WeasyPrint) is a
one-template change.

Wired up to the CLI as ``vmaf-tune report …``. The CLI orchestrates
the upstream phases (ladder + compare + optional per-shot) and hands
the structured outputs here; this module never invokes the encoder or
the libvmaf CLI directly.

The renderer is intentionally pure data → string. Matplotlib is the
only heavy dependency and is imported lazily so unrelated CLI paths
don't pay the import cost.
"""

from __future__ import annotations

import base64
import dataclasses
import io
import json
import math
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from . import __version__ as TOOL_VERSION


def _nan_to_none(value: Any) -> Any:
    """Recursively substitute ``None`` for ``NaN`` / ``±inf`` floats.

    The report appendix used to dump ``data.to_dict()`` through the
    default ``json.dumps`` (``allow_nan=True``), which writes bare
    ``NaN`` tokens for any in-memory ``float('nan')``. Those tokens
    are valid only under JavaScript-extended JSON; RFC 8259 strict
    parsers (Go ``encoding/json``, Rust ``serde_json``, ``jq``)
    reject them. Failed compare/ladder rows surface as in-memory
    NaNs even when the upstream JSON files write them as ``null``
    — the dump round-trips ``null -> float('nan')`` on parse and
    back to ``NaN`` on emit. Coerce to ``None`` here so the
    appendix is portable JSON (BBB e2e v2 Bug #v2-D, ADR-0498).
    """
    if isinstance(value, float):
        return None if math.isnan(value) or math.isinf(value) else value
    if isinstance(value, dict):
        return {k: _nan_to_none(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_nan_to_none(v) for v in value]
    return value


def _portable_json_dump(data: Any) -> str:
    """``json.dumps`` with NaN coercion + ``allow_nan=False`` defence."""
    return json.dumps(_nan_to_none(data), indent=2, sort_keys=True, allow_nan=False)


@dataclasses.dataclass(frozen=True)
class CodecRow:
    """One row in the codec-comparison table."""

    codec: str
    encoder_version: str
    best_crf: int
    bitrate_kbps: float
    encode_time_ms: float
    vmaf_score: float
    ok: bool
    error: str = ""


@dataclasses.dataclass(frozen=True)
class LadderRung:
    """One rung of the final ABR ladder."""

    width: int
    height: int
    bitrate_kbps: float
    vmaf: float
    crf: int


@dataclasses.dataclass(frozen=True)
class LadderSample:
    """One sampled (resolution, vmaf, bitrate, crf) point."""

    width: int
    height: int
    bitrate_kbps: float
    vmaf: float
    crf: int


@dataclasses.dataclass(frozen=True)
class ShotRow:
    """One per-shot tune result."""

    shot_index: int
    start_frame: int
    end_frame: int
    width: int
    height: int
    best_crf: int
    vmaf: float
    bitrate_kbps: float
    duration_s: float


@dataclasses.dataclass(frozen=True)
class SourceInfo:
    """Metadata about the source video."""

    path: str
    width: int
    height: int
    fps: float
    duration_s: float
    frame_count: int
    codec: str
    size_bytes: int


@dataclasses.dataclass(frozen=True)
class BisectSamplePoint:
    """One per-iteration (CRF, bitrate, VMAF) probe from a target-VMAF bisect.

    Mirrors :class:`vmaftune.bisect.BisectSample` on the report side so
    the renderer does not import the bisect module (keeps ``report``
    free of subprocess / ffmpeg deps).
    """

    crf: int
    bitrate_kbps: float
    vmaf_score: float
    encode_time_ms: float = 0.0


@dataclasses.dataclass(frozen=True)
class CodecSweepPoint:
    """One (codec, target_vmaf) point in a rate-quality sweep.

    The renderer builds the per-codec rate-quality line from these
    points (one line per codec, ordered by ``target_vmaf``) and
    derives the pareto frontier — for each target VMAF, the codec
    with the smallest bitrate.

    Schema v2 ingestion (ADR-0516). The legacy single-target compare
    output (schema v1) ingests as :class:`CodecRow` and renders the
    historical bar+dot chart for back-compat.

    ``bisect_samples`` (ADR-0530, schema v2 additive) carries every
    encode+score probe the underlying bisect walked through. When
    populated, the rate-quality chart aggregates these per codec
    (across all targets) and draws a single monotonic R-Q curve
    instead of connecting the picked-CRF cells. Empty tuple = old v2
    JSON without samples — renderer falls back to the legacy
    connect-the-dots line with a caveat note.
    """

    codec: str
    encoder_version: str
    target_vmaf: float
    best_crf: int
    bitrate_kbps: float
    encode_time_ms: float
    vmaf_score: float
    ok: bool
    error: str = ""
    bisect_samples: tuple[BisectSamplePoint, ...] = ()


@dataclasses.dataclass(frozen=True)
class ReportData:
    """All structured inputs for one report."""

    source: SourceInfo
    target_vmaf: float
    codec_rows: tuple[CodecRow, ...] = ()
    sweep_points: tuple[CodecSweepPoint, ...] = ()
    sweep_targets: tuple[float, ...] = ()
    ladder_samples: tuple[LadderSample, ...] = ()
    ladder_rungs: tuple[LadderRung, ...] = ()
    shots: tuple[ShotRow, ...] = ()
    tool_version: str = TOOL_VERSION
    generated_at_iso: str = ""

    def to_dict(self) -> dict[str, Any]:
        """Serialisable view of the structured inputs.

        Used by the HTML/MD templates and any external consumer that
        wants to re-render or diff two reports.
        """
        return {
            "source": dataclasses.asdict(self.source),
            "target_vmaf": self.target_vmaf,
            "codec_rows": [dataclasses.asdict(r) for r in self.codec_rows],
            "sweep_points": [dataclasses.asdict(p) for p in self.sweep_points],
            "sweep_targets": list(self.sweep_targets),
            "ladder_samples": [dataclasses.asdict(p) for p in self.ladder_samples],
            "ladder_rungs": [dataclasses.asdict(r) for r in self.ladder_rungs],
            "shots": [dataclasses.asdict(s) for s in self.shots],
            "tool_version": self.tool_version,
            "generated_at_iso": self.generated_at_iso,
        }


# ---------------------------------------------------------------------------
# Charts (lazy matplotlib import)
# ---------------------------------------------------------------------------


def _render_chart(width_in: float, height_in: float, plot_fn) -> bytes:
    """Render a matplotlib figure to PNG bytes.

    ``plot_fn`` is called with one ``Axes`` argument. The figure is
    closed after rendering so callers don't accumulate global state.

    Returns empty bytes when matplotlib is not importable. ADR-0498
    pairs this fallback with the Containerfile pin so the table-only
    report renders even on hosts that didn't pip-install matplotlib
    (Bug #v2-C, BBB e2e v2 2026-05-18).
    """
    try:
        import matplotlib  # noqa: PLC0415
    except ImportError:
        return b""

    matplotlib.use("Agg", force=True)
    import matplotlib.pyplot as plt  # noqa: PLC0415

    fig, ax = plt.subplots(figsize=(width_in, height_in), dpi=110)
    try:
        plot_fn(ax)
        buf = io.BytesIO()
        fig.savefig(buf, format="png", bbox_inches="tight")
        return buf.getvalue()
    finally:
        plt.close(fig)


def _render_chart_svg(width_in: float, height_in: float, plot_fn) -> str:
    """Render a matplotlib figure to SVG string for inline-HTML use.

    Returns an empty string when matplotlib is unavailable so the
    surrounding HTML still renders without the chart panel
    (ADR-0498, Bug #v2-C fallback companion).
    """
    try:
        import matplotlib  # noqa: PLC0415
    except ImportError:
        return ""

    matplotlib.use("Agg", force=True)
    import matplotlib.pyplot as plt  # noqa: PLC0415

    fig, ax = plt.subplots(figsize=(width_in, height_in), dpi=110)
    try:
        plot_fn(ax)
        buf = io.StringIO()
        fig.savefig(buf, format="svg", bbox_inches="tight")
        return buf.getvalue()
    finally:
        plt.close(fig)


def _ladder_plot_fn(data: ReportData):
    def _plot(ax) -> None:
        if data.ladder_samples:
            xs = [p.bitrate_kbps for p in data.ladder_samples]
            ys = [p.vmaf for p in data.ladder_samples]
            ax.scatter(xs, ys, s=12, alpha=0.45, label=f"samples ({len(xs)})")
        if data.ladder_rungs:
            xs = [r.bitrate_kbps for r in data.ladder_rungs]
            ys = [r.vmaf for r in data.ladder_rungs]
            ax.plot(xs, ys, marker="o", linewidth=2, color="#d62728", label="picked rungs")
        ax.set_xlabel("bitrate (kbps)")
        ax.set_ylabel("VMAF")
        if data.target_vmaf:
            ax.axhline(
                data.target_vmaf,
                color="#1f77b4",
                linestyle="--",
                alpha=0.5,
                label=f"target {data.target_vmaf:.1f}",
            )
        ax.set_xscale("log")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="lower right", fontsize=8)
        ax.set_title("Bitrate vs VMAF (rate-distortion)")

    return _plot


def _codec_plot_fn(data: ReportData):
    def _plot(ax) -> None:
        # Drop failed AND non-finite-numeric rows so the bar / line
        # axes don't try to plot NaN (matplotlib would otherwise emit
        # an empty plot or a warning, depending on version). Bug #6.
        rows = [
            r
            for r in data.codec_rows
            if r.ok and not _is_missing(r.bitrate_kbps) and not _is_missing(r.vmaf_score)
        ]
        if not rows:
            ax.text(0.5, 0.5, "no successful codec rows", ha="center", va="center")
            ax.set_axis_off()
            return
        codecs = [r.codec for r in rows]
        bitrates = [r.bitrate_kbps for r in rows]
        vmafs = [r.vmaf_score for r in rows]
        ax.bar(codecs, bitrates, color="#2ca02c")
        ax.set_ylabel("bitrate (kbps)")
        ax.set_title("Codec bitrate to reach target VMAF")
        ax2 = ax.twinx()
        ax2.plot(codecs, vmafs, "o-", color="#d62728", label="VMAF achieved")
        ax2.set_ylabel("VMAF")
        ax2.legend(loc="lower right", fontsize=8)
        for tick in ax.get_xticklabels():
            tick.set_rotation(20)

    return _plot


# Stable codec -> colour map so the same codec keeps the same hue in
# successive reports (avoids "blue swap" diffs when comparing two HTML
# reports side by side). Matplotlib's default Tab10 + Set2 give 18
# distinct hues — enough headroom for the CPU+HW encoder matrix.
_CODEC_PALETTE: tuple[str, ...] = (
    "#1f77b4",  # libx264
    "#ff7f0e",  # libx265
    "#2ca02c",  # libsvtav1
    "#d62728",  # libvpx-vp9
    "#9467bd",  # libaom
    "#8c564b",  # libvvenc
    "#e377c2",  # h264_nvenc
    "#7f7f7f",  # hevc_nvenc
    "#bcbd22",  # av1_nvenc
    "#17becf",  # h264_qsv
    "#aec7e8",  # hevc_qsv
    "#ffbb78",  # av1_qsv
    "#98df8a",  # h264_amf
    "#ff9896",  # hevc_amf
    "#c5b0d5",  # av1_amf
)


def _codec_colour(codec: str, index: int) -> str:
    """Deterministic colour for ``codec`` — falls back by index."""
    return _CODEC_PALETTE[index % len(_CODEC_PALETTE)]


def compute_pareto_frontier(
    points: Sequence[CodecSweepPoint],
) -> tuple[CodecSweepPoint, ...]:
    """Return the bitrate-minimising frontier across all codecs.

    For each target VMAF that appears in ``points``, pick the ok-row
    with the smallest bitrate (any codec). The frontier is ordered by
    ``target_vmaf`` ascending. Failed / non-finite rows are filtered
    out so the renderer never draws a NaN segment.
    """
    by_target: dict[float, list[CodecSweepPoint]] = {}
    for p in points:
        if not p.ok or _is_missing(p.bitrate_kbps) or _is_missing(p.vmaf_score):
            continue
        by_target.setdefault(p.target_vmaf, []).append(p)
    frontier: list[CodecSweepPoint] = []
    for target in sorted(by_target):
        # Tie-break by codec name so two-codec ties are deterministic.
        winner = min(by_target[target], key=lambda r: (r.bitrate_kbps, r.codec))
        frontier.append(winner)
    return tuple(frontier)


def _has_bisect_samples(points: Sequence[CodecSweepPoint]) -> bool:
    """True when at least one ok point carries a populated samples tuple.

    ADR-0530: gates the new chart variant. Old v2 dumps without samples
    plus all v1 dumps fall through to the legacy connect-the-dots line
    with a caveat note.
    """
    for p in points:
        if p.ok and p.bisect_samples:
            return True
    return False


def _dedup_bisect_samples(
    points: Sequence[CodecSweepPoint],
) -> dict[str, list[BisectSamplePoint]]:
    """Aggregate per-codec bisect samples, de-duplicated by CRF.

    The same codec is bisected once per target — those bisect probes
    overlap (e.g. a target-85 bisect and a target-90 bisect on libx264
    will both visit CRF 24 if that's a midpoint). Two measurements at
    the same CRF on the same source should be close to identical, so
    we keep the first occurrence and drop subsequent duplicates. Sorted
    by bitrate ascending so the rendered curve is monotonic-friendly.
    """
    by_codec: dict[str, dict[int, BisectSamplePoint]] = {}
    for p in points:
        if not p.ok:
            continue
        for s in p.bisect_samples:
            if _is_missing(s.bitrate_kbps) or _is_missing(s.vmaf_score):
                continue
            by_codec.setdefault(p.codec, {}).setdefault(int(s.crf), s)
    out: dict[str, list[BisectSamplePoint]] = {}
    for codec, by_crf in by_codec.items():
        samples = list(by_crf.values())
        samples.sort(key=lambda s: s.bitrate_kbps)
        out[codec] = samples
    return out


def _sweep_plot_fn(data: ReportData):
    """Rate-quality line plot — one curve per codec, pareto highlighted.

    Two modes (ADR-0530):

    - **From-bisect-samples** (schema-v2 with ``bisect_samples``):
      each codec's curve is built from every CRF the underlying
      target-VMAF bisects probed. Sorted by bitrate, drawn as a
      monotonic-friendly line; picked-CRF rows are highlighted as
      larger filled markers on top of the line. Pareto frontier of
      picked-CRF rows is the dashed overlay.
    - **Legacy connect-the-dots** (schema-v1, or v2 without samples):
      one line per codec connecting its picked-CRF rows by target.
      Carries a caveat note in the title because the bisect's
      target-overshoot can flip the apparent VMAF order between
      adjacent targets — that's the bug ADR-0530 fixes for the new
      mode.
    """

    def _plot(ax) -> None:
        if not data.sweep_points:
            ax.text(0.5, 0.5, "no sweep data", ha="center", va="center")
            ax.set_axis_off()
            return
        use_samples = _has_bisect_samples(data.sweep_points)
        plotted_any = False
        if use_samples:
            curves = _dedup_bisect_samples(data.sweep_points)
            picked_by_codec: dict[str, list[CodecSweepPoint]] = {}
            for p in data.sweep_points:
                if not p.ok or _is_missing(p.bitrate_kbps) or _is_missing(p.vmaf_score):
                    continue
                picked_by_codec.setdefault(p.codec, []).append(p)
            for idx, (codec, samples) in enumerate(sorted(curves.items())):
                if not samples:
                    continue
                colour = _codec_colour(codec, idx)
                first_picked = picked_by_codec.get(codec, [])
                ver = first_picked[0].encoder_version if first_picked else ""
                label = f"{codec} ({ver})" if ver else codec
                xs = [s.bitrate_kbps for s in samples]
                ys = [s.vmaf_score for s in samples]
                # Bisect-sample curve: small markers, thin line — the
                # genuine codec R-Q signal.
                ax.plot(
                    xs,
                    ys,
                    marker="o",
                    markersize=3.5,
                    linewidth=1.2,
                    color=colour,
                    alpha=0.85,
                    label=label,
                )
                # Picked-CRF overlay: larger hollow markers so the eye
                # spots them as the actual per-target winners.
                if first_picked:
                    pxs = [p.bitrate_kbps for p in first_picked]
                    pys = [p.vmaf_score for p in first_picked]
                    ax.scatter(
                        pxs,
                        pys,
                        s=80,
                        facecolors="none",
                        edgecolors=colour,
                        linewidths=1.8,
                        zorder=8,
                    )
                plotted_any = True
        else:
            by_codec: dict[str, list[CodecSweepPoint]] = {}
            for p in data.sweep_points:
                by_codec.setdefault(p.codec, []).append(p)
            for idx, (codec, pts) in enumerate(sorted(by_codec.items())):
                ok_pts = [
                    p
                    for p in pts
                    if p.ok and not _is_missing(p.bitrate_kbps) and not _is_missing(p.vmaf_score)
                ]
                if not ok_pts:
                    continue
                ok_pts.sort(key=lambda p: p.target_vmaf)
                xs = [p.bitrate_kbps for p in ok_pts]
                ys = [p.vmaf_score for p in ok_pts]
                label = codec
                ver = ok_pts[0].encoder_version
                if ver:
                    label = f"{codec} ({ver})"
                ax.plot(
                    xs,
                    ys,
                    marker="o",
                    linewidth=1.6,
                    markersize=6,
                    color=_codec_colour(codec, idx),
                    label=label,
                )
                plotted_any = True
        # Pareto frontier overlay (always from the picked-CRF rows).
        frontier = compute_pareto_frontier(data.sweep_points)
        if frontier:
            fxs = [p.bitrate_kbps for p in frontier]
            fys = [p.vmaf_score for p in frontier]
            ax.plot(
                fxs,
                fys,
                color="#000",
                linestyle="--",
                linewidth=2.4,
                alpha=0.55,
                label="pareto frontier",
                zorder=10,
            )
            for p in frontier:
                ax.annotate(
                    f"{p.codec}",
                    xy=(p.bitrate_kbps, p.vmaf_score),
                    xytext=(5, 5),
                    textcoords="offset points",
                    fontsize=7,
                    color="#000",
                    alpha=0.7,
                )
        if not plotted_any:
            ax.text(0.5, 0.5, "no successful sweep rows", ha="center", va="center")
            ax.set_axis_off()
            return
        ax.set_xscale("log")
        ax.set_xlabel("bitrate (kbps, log scale)")
        ax.set_ylabel("VMAF achieved")
        if use_samples:
            ax.set_title(
                "Rate-quality curve per codec (bisect samples; "
                "picked-CRF circled; pareto frontier dashed)"
            )
        else:
            ax.set_title(
                "Rate-quality curve per codec (pareto frontier dashed) — "
                "caveat: connect-the-dots may show overshoot artefacts"
            )
        ax.grid(True, alpha=0.3, which="both")
        # Horizontal reference lines at each sweep target.
        for t in data.sweep_targets:
            ax.axhline(t, color="#888", linestyle=":", alpha=0.35, linewidth=0.8)
        ax.legend(loc="lower right", fontsize=7)

    return _plot


def _shot_plot_fn(data: ReportData):
    def _plot(ax) -> None:
        if not data.shots:
            ax.text(0.5, 0.5, "no per-shot data", ha="center", va="center")
            ax.set_axis_off()
            return
        ax2 = ax.twinx()
        # ADR-0512 Bug B: when the source resolves to a single shot the
        # historical ``ax.step([start], [crf], ...)`` produced a
        # zero-length path that the SVG backend silently dropped, leaving
        # the chart visually empty even though the axes/title rendered.
        # Render each shot as a horizontal band over its frame range so
        # the user always sees a drawable element — for the 1-shot case
        # this becomes a single full-width band labelled with the
        # tuner's pick.
        for shot in data.shots:
            x0 = shot.start_frame
            x1 = shot.end_frame
            ax.hlines(
                shot.best_crf,
                x0,
                x1,
                colors="#1f77b4",
                linewidth=2.5,
            )
            ax2.hlines(
                shot.vmaf,
                x0,
                x1,
                colors="#d62728",
                linewidth=2.5,
                alpha=0.7,
            )
            # Midpoint markers — extra insurance that the segment is
            # visible even when the band collapses on a print-at-small
            # output size.
            mid = (x0 + x1) / 2.0
            ax.plot(mid, shot.best_crf, marker="o", color="#1f77b4", markersize=4)
            ax2.plot(mid, shot.vmaf, marker="o", color="#d62728", markersize=4, alpha=0.7)
        # Provide explicit axis ranges so a single-shot dataset still
        # picks sensible bounds (matplotlib's autoscale degenerates on a
        # single point + hline pair at the same coordinate).
        #
        # ADR-0531 Bug B: use 1.05 * last_end (right side) + 0.02-span
        # left-side padding so the last shot's CRF band is always fully
        # inside the axes viewport. When last_end is exactly on the axes
        # right boundary, matplotlib's clip box trims the rightmost pixel
        # of the hlines artist, making the final shot's band appear
        # invisible at small output sizes.
        last_end = max(shot.end_frame for shot in data.shots)
        first_start = min(shot.start_frame for shot in data.shots)
        x_pad_left = max(1.0, 0.02 * (last_end - first_start))
        x_pad_right = max(1.0, 0.05 * last_end)
        ax.set_xlim(first_start - x_pad_left, last_end + x_pad_right)
        crfs = [shot.best_crf for shot in data.shots]
        vmafs = [shot.vmaf for shot in data.shots]
        if min(crfs) == max(crfs):
            ax.set_ylim(min(crfs) - 2.0, max(crfs) + 2.0)
        if min(vmafs) == max(vmafs):
            ax2.set_ylim(min(vmafs) - 1.0, min(100.0, max(vmafs) + 1.0))
        # Synthetic legend handles — matplotlib's auto-legend skips
        # hline artists in older versions; the proxies keep the legend
        # populated regardless of backend version.
        from matplotlib.lines import Line2D  # noqa: PLC0415

        ax.legend(
            handles=[Line2D([0], [0], color="#1f77b4", lw=2.5, label="best CRF")],
            loc="upper left",
            fontsize=8,
        )
        ax2.legend(
            handles=[Line2D([0], [0], color="#d62728", lw=2.5, alpha=0.7, label="VMAF")],
            loc="upper right",
            fontsize=8,
        )
        ax.set_xlabel("frame")
        ax.set_ylabel("CRF")
        ax2.set_ylabel("VMAF")
        ax.set_title("Per-shot tuning timeline")
        ax.grid(True, alpha=0.3)

    return _plot


# ---------------------------------------------------------------------------
# Markdown rendering
# ---------------------------------------------------------------------------


def _fmt_bytes(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if n < 1024 or unit == "TiB":
            return f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n:.1f} TiB"


_DASH = "—"  # U+2014 em-dash; renderer placeholder for NaN / None.


def _is_missing(v: float | None) -> bool:
    """Return True when ``v`` is ``None`` or a non-finite float.

    Failed bisect rows (Bug #3) propagate ``NaN`` through the compare
    JSON; the renderer formats them as an em-dash so the profile-card
    table no longer prints literal ``nan kbps`` / ``nan`` text
    (Bug #6, BBB e2e 2026-05-17).
    """
    if v is None:
        return True
    return math.isnan(v) or math.isinf(v)


def _fmt_kbps(v: float | None) -> str:
    if _is_missing(v):
        return _DASH
    v = float(v)  # type: ignore[arg-type]
    if v >= 1000:
        return f"{v / 1000:.2f} Mbps"
    return f"{v:.0f} kbps"


def _fmt_duration(s: float | None) -> str:
    if _is_missing(s):
        return _DASH
    s = float(s)  # type: ignore[arg-type]
    if s < 60:
        return f"{s:.1f}s"
    m, sec = divmod(int(s), 60)
    return f"{m}m {sec}s"


def _fmt_vmaf(v: float | None) -> str:
    if _is_missing(v):
        return _DASH
    return f"{float(v):.2f}"


def _fmt_ms(v: float | None) -> str:
    if _is_missing(v):
        return _DASH
    return f"{float(v):.0f} ms"


def _fmt_crf(v: int | None) -> str:
    if v is None or v < 0:
        return _DASH
    return str(int(v))


def _per_codec_targets(
    points: Sequence[CodecSweepPoint], targets: Sequence[float]
) -> dict[str, dict[float, CodecSweepPoint]]:
    """Group sweep points by (codec, target_vmaf) for table rendering."""
    by_codec: dict[str, dict[float, CodecSweepPoint]] = {}
    for p in points:
        by_codec.setdefault(p.codec, {})[p.target_vmaf] = p
    # Ensure every codec has a slot for every requested target so the
    # summary table prints a uniform shape (missing cells render as
    # em-dashes for unavailable encoders / failed bisects).
    for codec in by_codec:
        for t in targets:
            by_codec[codec].setdefault(t, None)  # type: ignore[assignment]
    return by_codec


def _render_sweep_summary_table_md(data: ReportData) -> list[str]:
    """Render the per-codec / per-target summary table as markdown."""
    targets = list(data.sweep_targets) or sorted({p.target_vmaf for p in data.sweep_points})
    by_codec = _per_codec_targets(data.sweep_points, targets)

    header_cells = ["Codec", "Encoder"]
    header_cells.extend(f"bitrate @ VMAF {t:g}" for t in targets)
    header_cells.extend(["encode time (ms / frame)", "best preset"])
    align = ["---", "---"] + ["---:" for _ in targets] + ["---:", "---"]

    lines: list[str] = []
    lines.append("| " + " | ".join(header_cells) + " |")
    lines.append("|" + "|".join(align) + "|")
    for codec in sorted(by_codec):
        per_target = by_codec[codec]
        rows = [p for p in per_target.values() if p is not None]
        ok_rows = [p for p in rows if p.ok]
        encoder_version = next((p.encoder_version for p in rows if p.encoder_version), "—")
        cells: list[str] = [codec, encoder_version]
        for t in targets:
            p = per_target.get(t)
            if p is None or not p.ok or _is_missing(p.bitrate_kbps):
                cells.append(_DASH)
            else:
                cells.append(_fmt_kbps(p.bitrate_kbps))
        # Encode time per frame: average across the codec's ok rows.
        if ok_rows:
            avg_ms = sum(p.encode_time_ms for p in ok_rows) / float(len(ok_rows))
            cells.append(_fmt_ms(avg_ms))
        else:
            cells.append(_DASH)
        cells.append("adapter default")
        lines.append("| " + " | ".join(cells) + " |")
    # Pareto-frontier summary.
    frontier = compute_pareto_frontier(data.sweep_points)
    if frontier:
        lines.append("")
        lines.append("**Pareto frontier** (lowest bitrate at each target):")
        lines.append("")
        for p in frontier:
            lines.append(
                f"- VMAF {p.target_vmaf:g}: `{p.codec}` "
                f"({p.encoder_version or '—'}) "
                f"@ {_fmt_kbps(p.bitrate_kbps)} (CRF {_fmt_crf(p.best_crf)})"
            )
    return lines


def render_markdown(data: ReportData, *, assets_dir: Path | None = None) -> str:
    """Render the report to Markdown.

    If ``assets_dir`` is provided, PNG charts are written under it and
    referenced via relative paths. If omitted, charts are inlined as
    base64 data URLs (less diff-friendly but single-file).
    """
    lines: list[str] = []
    src = data.source

    lines.append(f"# vmaf-tune report — `{Path(src.path).name}`")
    lines.append("")
    lines.append(
        f"_Generated by vmaf-tune {data.tool_version}{' on ' + data.generated_at_iso if data.generated_at_iso else ''}._"
    )
    lines.append("")
    lines.append("## Source")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("|---|---|")
    lines.append(f"| Path | `{src.path}` |")
    lines.append(f"| Resolution | {src.width} × {src.height} |")
    lines.append(f"| Frame rate | {src.fps:.3f} fps |")
    lines.append(f"| Duration | {_fmt_duration(src.duration_s)} ({src.frame_count} frames) |")
    lines.append(f"| Codec | {src.codec} |")
    lines.append(f"| File size | {_fmt_bytes(src.size_bytes)} |")
    lines.append("")
    lines.append(f"Target VMAF: **{data.target_vmaf:.1f}**")
    lines.append("")

    # Codec comparison table + chart. Schema v2 (sweep_points) takes
    # priority — when both are present (legacy + new in the same
    # report), the sweep view is the useful one and the bar+dot chart
    # is informational only.
    if data.sweep_points:
        lines.append("## Codec rate-quality sweep")
        lines.append("")
        lines.extend(_render_sweep_summary_table_md(data))
        lines.append("")
        lines.append(
            _embed_png(_render_chart(8, 4.5, _sweep_plot_fn(data)), "sweep_rq", assets_dir)
        )
        lines.append("")
    elif data.codec_rows:
        lines.append("## Codec comparison")
        lines.append("")
        lines.append("| Codec | Encoder | CRF | Bitrate | Encode time | VMAF | Status |")
        lines.append("|---|---|---:|---:|---:|---:|---|")
        for r in data.codec_rows:
            status = "✓" if r.ok else f"✗ {r.error}"
            lines.append(
                f"| {r.codec} | {r.encoder_version or '—'} | {_fmt_crf(r.best_crf)} | "
                f"{_fmt_kbps(r.bitrate_kbps)} | {_fmt_ms(r.encode_time_ms)} | "
                f"{_fmt_vmaf(r.vmaf_score)} | {status} |"
            )
        lines.append("")
        lines.append(
            _embed_png(_render_chart(7, 3.5, _codec_plot_fn(data)), "codec_compare", assets_dir)
        )
        lines.append("")

    # Ladder
    if data.ladder_rungs or data.ladder_samples:
        lines.append("## ABR ladder")
        lines.append("")
        if data.ladder_rungs:
            lines.append("Selected rungs:")
            lines.append("")
            lines.append("| Resolution | CRF | Bitrate | VMAF |")
            lines.append("|---|---:|---:|---:|")
            for r in data.ladder_rungs:
                lines.append(
                    f"| {r.width}×{r.height} | {_fmt_crf(r.crf)} | "
                    f"{_fmt_kbps(r.bitrate_kbps)} | {_fmt_vmaf(r.vmaf)} |"
                )
            lines.append("")
        lines.append(_embed_png(_render_chart(7, 4, _ladder_plot_fn(data)), "ladder", assets_dir))
        lines.append("")

    # Per-shot
    if data.shots:
        lines.append("## Per-shot tuning")
        lines.append("")
        lines.append(f"{len(data.shots)} shots detected.")
        lines.append("")
        lines.append("| # | Frames | Duration | Resolution | Best CRF | VMAF | Bitrate |")
        lines.append("|---:|---:|---:|---|---:|---:|---:|")
        for s in data.shots:
            lines.append(
                f"| {s.shot_index} | {s.start_frame}–{s.end_frame} | "
                f"{_fmt_duration(s.duration_s)} | {s.width}×{s.height} | "
                f"{_fmt_crf(s.best_crf)} | {_fmt_vmaf(s.vmaf)} | "
                f"{_fmt_kbps(s.bitrate_kbps)} |"
            )
        lines.append("")
        lines.append(_embed_png(_render_chart(7, 3.5, _shot_plot_fn(data)), "shots", assets_dir))
        lines.append("")

    lines.append("---")
    lines.append("")
    lines.append("Raw JSON dump for downstream tooling:")
    lines.append("")
    lines.append("<details><summary>report.json</summary>")
    lines.append("")
    lines.append("```json")
    lines.append(_portable_json_dump(data.to_dict()))
    lines.append("```")
    lines.append("")
    lines.append("</details>")
    return "\n".join(lines) + "\n"


def _embed_png(png: bytes, name: str, assets_dir: Path | None) -> str:
    if not png:
        # Matplotlib unavailable (ADR-0498 fallback); skip the image
        # rather than emit a broken inline data URI.
        return f"<!-- chart {name} unavailable (matplotlib not installed) -->"
    if assets_dir is None:
        b64 = base64.b64encode(png).decode("ascii")
        return f"![{name}](data:image/png;base64,{b64})"
    assets_dir.mkdir(parents=True, exist_ok=True)
    target = assets_dir / f"{name}.png"
    target.write_bytes(png)
    return f"![{name}]({target.relative_to(assets_dir.parent) if assets_dir.parent in target.parents else target.name})"


# ---------------------------------------------------------------------------
# HTML rendering
# ---------------------------------------------------------------------------


_HTML_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>vmaf-tune report — {source_name}</title>
<style>
:root {{
    --bg: #0f1115; --panel: #161823; --text: #e6e7ea; --muted: #c2c5cc;
    --accent: #f5792a; --link: #9bd2ff; --ok: #4caf50; --bad: #ef5350;
}}
@media (prefers-color-scheme: light) {{
    :root {{
        --bg: #fbfbfd; --panel: #fff; --text: #1d2433;
        --muted: #51607a; --accent: #ee5a24; --link: #005bbb;
    }}
}}
body {{ background: var(--bg); color: var(--text); font: 16px/1.5 -apple-system, "Segoe UI", system-ui, sans-serif;
        max-width: 1100px; margin: 0 auto; padding: 2rem 1.25rem; }}
h1 {{ color: var(--accent); border-bottom: 2px solid var(--accent); padding-bottom: .4rem; }}
h2 {{ margin-top: 2rem; }}
.panel {{ background: var(--panel); border-radius: 8px; padding: 1.2rem; margin: 1rem 0; }}
table {{ width: 100%; border-collapse: collapse; margin: .5rem 0; }}
th, td {{ padding: .4rem .6rem; text-align: left; border-bottom: 1px solid #2d3142; }}
th {{ color: var(--muted); font-weight: 600; font-size: .85rem; text-transform: uppercase; letter-spacing: .03em; }}
td.num {{ text-align: right; font-variant-numeric: tabular-nums; }}
.tag {{ display: inline-block; padding: .15em .5em; border-radius: 3px; font-size: .85em; }}
.tag.ok {{ background: rgba(76,175,80,.18); color: var(--ok); }}
.tag.bad {{ background: rgba(239,83,80,.18); color: var(--bad); }}
.chart {{ background: var(--panel); border-radius: 8px; padding: .5rem; overflow-x: auto; }}
.chart svg {{ max-width: 100%; height: auto; display: block; margin: 0 auto; }}
.meta {{ color: var(--muted); font-size: .9rem; margin-bottom: 1.5rem; }}
details {{ background: var(--panel); padding: .8rem 1.2rem; border-radius: 8px; margin-top: 2rem; }}
details summary {{ cursor: pointer; color: var(--muted); }}
pre {{ background: #000; color: #ddd; padding: 1rem; border-radius: 6px; overflow-x: auto; font-size: 12px; }}
.kv {{ display: grid; grid-template-columns: 12rem 1fr; gap: .3rem 1rem; }}
.kv .key {{ color: var(--muted); }}
.target {{ color: var(--accent); font-size: 1.1em; font-weight: 600; }}
</style>
</head>
<body>
<h1>vmaf-tune report — {source_name}</h1>
<div class="meta">Generated by vmaf-tune {tool_version}{generated_at}.</div>

<div class="panel">
<h2 style="margin-top:0">Source</h2>
<div class="kv">
<div class="key">Path</div><div><code>{source_path}</code></div>
<div class="key">Resolution</div><div>{width} × {height}</div>
<div class="key">Frame rate</div><div>{fps:.3f} fps</div>
<div class="key">Duration</div><div>{duration} ({frame_count} frames)</div>
<div class="key">Codec</div><div>{codec}</div>
<div class="key">File size</div><div>{size}</div>
<div class="key">Target VMAF</div><div class="target">{target_vmaf:.1f}</div>
</div>
</div>

{codec_section}
{ladder_section}
{shots_section}

<details>
<summary>Raw JSON dump (for downstream tooling)</summary>
<pre>{json_dump}</pre>
</details>
</body>
</html>
"""


def _row_html(row: CodecRow) -> str:
    status = (
        '<span class="tag ok">OK</span>'
        if row.ok
        else f'<span class="tag bad">{row.error or "fail"}</span>'
    )
    return (
        f"<tr><td>{row.codec}</td><td>{row.encoder_version or '—'}</td>"
        f"<td class='num'>{_fmt_crf(row.best_crf)}</td>"
        f"<td class='num'>{_fmt_kbps(row.bitrate_kbps)}</td>"
        f"<td class='num'>{_fmt_ms(row.encode_time_ms)}</td>"
        f"<td class='num'>{_fmt_vmaf(row.vmaf_score)}</td>"
        f"<td>{status}</td></tr>"
    )


def _sweep_summary_table_html(data: ReportData) -> str:
    """Render the per-codec / per-target summary table as HTML."""
    targets = list(data.sweep_targets) or sorted({p.target_vmaf for p in data.sweep_points})
    by_codec = _per_codec_targets(data.sweep_points, targets)

    head_cells = ["Codec", "Encoder"]
    head_cells.extend(f"@ VMAF {t:g}" for t in targets)
    head_cells.extend(["Encode time", "Best preset"])
    head = "".join(f"<th>{c}</th>" for c in head_cells)

    body_rows: list[str] = []
    for codec in sorted(by_codec):
        per_target = by_codec[codec]
        rows = [p for p in per_target.values() if p is not None]
        ok_rows = [p for p in rows if p.ok]
        encoder_version = next((p.encoder_version for p in rows if p.encoder_version), "—")
        cells: list[str] = [f"<td>{codec}</td>", f"<td>{encoder_version}</td>"]
        for t in targets:
            p = per_target.get(t)
            if p is None or not p.ok or _is_missing(p.bitrate_kbps):
                cells.append(f"<td class='num'>{_DASH}</td>")
            else:
                cells.append(f"<td class='num'>{_fmt_kbps(p.bitrate_kbps)}</td>")
        if ok_rows:
            avg_ms = sum(p.encode_time_ms for p in ok_rows) / float(len(ok_rows))
            cells.append(f"<td class='num'>{_fmt_ms(avg_ms)}</td>")
        else:
            cells.append(f"<td class='num'>{_DASH}</td>")
        cells.append("<td>adapter default</td>")
        body_rows.append("<tr>" + "".join(cells) + "</tr>")

    table = f"<table><thead><tr>{head}</tr></thead>" f"<tbody>{''.join(body_rows)}</tbody></table>"

    frontier = compute_pareto_frontier(data.sweep_points)
    frontier_html = ""
    if frontier:
        items = "".join(
            f"<li>VMAF {p.target_vmaf:g}: <code>{p.codec}</code> "
            f"({p.encoder_version or '—'}) "
            f"@ {_fmt_kbps(p.bitrate_kbps)} (CRF {_fmt_crf(p.best_crf)})</li>"
            for p in frontier
        )
        frontier_html = (
            "<p><strong>Pareto frontier</strong> "
            "(lowest bitrate at each target):</p>"
            f"<ul>{items}</ul>"
        )
    return table + frontier_html


def render_html(data: ReportData) -> str:
    """Render the report to a single self-contained HTML file."""
    src = data.source
    codec_section = ""
    if data.sweep_points:
        # Schema v2 — rate-quality sweep takes priority over the
        # legacy bar+dot view when both are present (the latter is at
        # most as informative as the sweep restricted to one target).
        table_html = _sweep_summary_table_html(data)
        chart = _render_chart_svg(8, 4.5, _sweep_plot_fn(data))
        codec_section = (
            f"<div class='panel'><h2 style='margin-top:0'>Codec rate-quality sweep</h2>"
            f"{table_html}"
            f"<div class='chart'>{chart}</div></div>"
        )
    elif data.codec_rows:
        rows = "\n".join(_row_html(r) for r in data.codec_rows)
        chart = _render_chart_svg(7, 3.5, _codec_plot_fn(data))
        codec_section = (
            f"<div class='panel'><h2 style='margin-top:0'>Codec comparison</h2>"
            f"<table><thead><tr><th>Codec</th><th>Encoder</th><th>CRF</th>"
            f"<th>Bitrate</th><th>Encode time</th><th>VMAF</th><th>Status</th></tr></thead>"
            f"<tbody>{rows}</tbody></table>"
            f"<div class='chart'>{chart}</div></div>"
        )

    ladder_section = ""
    if data.ladder_rungs or data.ladder_samples:
        rungs_html = ""
        if data.ladder_rungs:
            rungs = "".join(
                f"<tr><td>{r.width}×{r.height}</td>"
                f"<td class='num'>{_fmt_crf(r.crf)}</td>"
                f"<td class='num'>{_fmt_kbps(r.bitrate_kbps)}</td>"
                f"<td class='num'>{_fmt_vmaf(r.vmaf)}</td></tr>"
                for r in data.ladder_rungs
            )
            rungs_html = (
                f"<table><thead><tr><th>Resolution</th><th>CRF</th>"
                f"<th>Bitrate</th><th>VMAF</th></tr></thead>"
                f"<tbody>{rungs}</tbody></table>"
            )
        chart = _render_chart_svg(8, 4, _ladder_plot_fn(data))
        ladder_section = (
            f"<div class='panel'><h2 style='margin-top:0'>ABR ladder</h2>"
            f"{rungs_html}<div class='chart'>{chart}</div></div>"
        )

    shots_section = ""
    if data.shots:
        rows = "".join(
            f"<tr><td class='num'>{s.shot_index}</td>"
            f"<td>{s.start_frame}–{s.end_frame}</td>"
            f"<td class='num'>{_fmt_duration(s.duration_s)}</td>"
            f"<td>{s.width}×{s.height}</td>"
            f"<td class='num'>{_fmt_crf(s.best_crf)}</td>"
            f"<td class='num'>{_fmt_vmaf(s.vmaf)}</td>"
            f"<td class='num'>{_fmt_kbps(s.bitrate_kbps)}</td></tr>"
            for s in data.shots
        )
        chart = _render_chart_svg(8, 3.5, _shot_plot_fn(data))
        shots_section = (
            f"<div class='panel'><h2 style='margin-top:0'>Per-shot tuning ({len(data.shots)} shots)</h2>"
            f"<table><thead><tr><th>#</th><th>Frames</th><th>Duration</th>"
            f"<th>Resolution</th><th>Best CRF</th><th>VMAF</th><th>Bitrate</th></tr></thead>"
            f"<tbody>{rows}</tbody></table>"
            f"<div class='chart'>{chart}</div></div>"
        )

    return _HTML_TEMPLATE.format(
        source_name=Path(src.path).name,
        source_path=src.path,
        width=src.width,
        height=src.height,
        fps=src.fps,
        duration=_fmt_duration(src.duration_s),
        frame_count=src.frame_count,
        codec=src.codec,
        size=_fmt_bytes(src.size_bytes),
        target_vmaf=data.target_vmaf,
        tool_version=data.tool_version,
        generated_at=f" on {data.generated_at_iso}" if data.generated_at_iso else "",
        codec_section=codec_section,
        ladder_section=ladder_section,
        shots_section=shots_section,
        json_dump=_portable_json_dump(data.to_dict()),
    )


# ---------------------------------------------------------------------------
# ffprobe helper
# ---------------------------------------------------------------------------


def probe_source(path: Path) -> SourceInfo:
    """Run ffprobe and return a :class:`SourceInfo`.

    Falls back to zero-filled values when ffprobe is unavailable; the
    caller should treat the result as best-effort.
    """
    import subprocess

    try:
        out = subprocess.check_output(
            [
                "ffprobe",
                "-v",
                "error",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=width,height,r_frame_rate,nb_frames,codec_name,duration",
                "-show_entries",
                "format=duration,size",
                "-of",
                "json",
                str(path),
            ],
            stderr=subprocess.STDOUT,
            timeout=30,
        )
    except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired):
        size = path.stat().st_size if path.exists() else 0
        return SourceInfo(
            path=str(path),
            width=0,
            height=0,
            fps=0.0,
            duration_s=0.0,
            frame_count=0,
            codec="unknown",
            size_bytes=size,
        )

    info = json.loads(out)
    stream = (info.get("streams") or [{}])[0]
    fmt = info.get("format") or {}
    fps_str = stream.get("r_frame_rate", "0/1")
    try:
        num, den = fps_str.split("/")
        fps = float(num) / float(den) if float(den) else 0.0
    except (ValueError, ZeroDivisionError):
        fps = 0.0
    duration = float(stream.get("duration") or fmt.get("duration") or 0.0)
    frame_count = int(stream.get("nb_frames") or 0) or int(duration * fps)
    return SourceInfo(
        path=str(path),
        width=int(stream.get("width") or 0),
        height=int(stream.get("height") or 0),
        fps=fps,
        duration_s=duration,
        frame_count=frame_count,
        codec=stream.get("codec_name") or "unknown",
        size_bytes=int(fmt.get("size") or (path.stat().st_size if path.exists() else 0)),
    )


__all__ = [
    "BisectSamplePoint",
    "CodecRow",
    "CodecSweepPoint",
    "LadderRung",
    "LadderSample",
    "ReportData",
    "ShotRow",
    "SourceInfo",
    "compute_pareto_frontier",
    "probe_source",
    "render_html",
    "render_markdown",
]
