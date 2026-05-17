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
from pathlib import Path
from typing import Any

from . import __version__ as TOOL_VERSION


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
class ReportData:
    """All structured inputs for one report."""

    source: SourceInfo
    target_vmaf: float
    codec_rows: tuple[CodecRow, ...] = ()
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
    """
    import matplotlib

    matplotlib.use("Agg", force=True)
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(width_in, height_in), dpi=110)
    try:
        plot_fn(ax)
        buf = io.BytesIO()
        fig.savefig(buf, format="png", bbox_inches="tight")
        return buf.getvalue()
    finally:
        plt.close(fig)


def _render_chart_svg(width_in: float, height_in: float, plot_fn) -> str:
    """Render a matplotlib figure to SVG string for inline-HTML use."""
    import matplotlib

    matplotlib.use("Agg", force=True)
    import matplotlib.pyplot as plt

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


def _shot_plot_fn(data: ReportData):
    def _plot(ax) -> None:
        if not data.shots:
            ax.text(0.5, 0.5, "no per-shot data", ha="center", va="center")
            ax.set_axis_off()
            return
        ax2 = ax.twinx()
        starts = [s.start_frame for s in data.shots]
        crfs = [s.best_crf for s in data.shots]
        vmafs = [s.vmaf for s in data.shots]
        ax.step(starts, crfs, where="post", color="#1f77b4", label="best CRF")
        ax.set_xlabel("frame")
        ax.set_ylabel("CRF")
        ax2.step(starts, vmafs, where="post", color="#d62728", alpha=0.7, label="VMAF")
        ax2.set_ylabel("VMAF")
        ax.set_title("Per-shot tuning timeline")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper left", fontsize=8)
        ax2.legend(loc="upper right", fontsize=8)

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

    # Codec comparison table + chart
    if data.codec_rows:
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
    lines.append(json.dumps(data.to_dict(), indent=2, sort_keys=True))
    lines.append("```")
    lines.append("")
    lines.append("</details>")
    return "\n".join(lines) + "\n"


def _embed_png(png: bytes, name: str, assets_dir: Path | None) -> str:
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


def render_html(data: ReportData) -> str:
    """Render the report to a single self-contained HTML file."""
    src = data.source
    codec_section = ""
    if data.codec_rows:
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
        json_dump=json.dumps(data.to_dict(), indent=2, sort_keys=True),
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
    "CodecRow",
    "LadderRung",
    "LadderSample",
    "ReportData",
    "ShotRow",
    "SourceInfo",
    "probe_source",
    "render_html",
    "render_markdown",
]
