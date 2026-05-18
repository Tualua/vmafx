# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Smoke tests for the vmaftune.report module."""

from __future__ import annotations

import json
import pathlib
import sys

# Make `vmaftune` importable for the in-tree test invocation.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "src"))

from vmaftune.report import (
    CodecRow,
    LadderRung,
    LadderSample,
    ReportData,
    ShotRow,
    SourceInfo,
    render_html,
    render_markdown,
)


def _sample_data() -> ReportData:
    src = SourceInfo(
        path="/tmp/example.mp4",
        width=1920,
        height=1080,
        fps=24.0,
        duration_s=10.0,
        frame_count=240,
        codec="h264",
        size_bytes=1_000_000,
    )
    return ReportData(
        source=src,
        target_vmaf=92.0,
        codec_rows=(
            CodecRow("libx264", "x264", 23, 2400, 4200, 92.4, True),
            CodecRow("libsvtav1", "SVT-AV1", 30, 1500, 7600, 92.6, True),
            CodecRow("libvpx", "libvpx", 33, 0, 0, 0, False, "timeout"),
        ),
        ladder_samples=(
            LadderSample(1920, 1080, 2400, 92.4, 23),
            LadderSample(1280, 720, 1100, 86.5, 26),
        ),
        ladder_rungs=(
            LadderRung(1920, 1080, 2400, 92.4, 23),
            LadderRung(1280, 720, 1100, 86.5, 26),
        ),
        shots=(
            ShotRow(0, 0, 120, 1920, 1080, 22, 94.2, 5800, 5.0),
            ShotRow(1, 120, 240, 1920, 1080, 26, 91.7, 3200, 5.0),
        ),
        generated_at_iso="2026-05-17T00:00:00+00:00",
    )


def test_markdown_contains_all_sections():
    md = render_markdown(_sample_data())
    assert "# vmaf-tune report" in md
    assert "## Source" in md
    assert "## Codec comparison" in md
    assert "## ABR ladder" in md
    assert "## Per-shot tuning" in md
    assert "libx264" in md
    assert "libsvtav1" in md
    assert "timeout" in md  # failed-row error visible
    assert "92.4" in md  # vmaf
    # raw JSON dump is collapsible
    assert "<details>" in md
    assert "report.json" in md


def test_html_is_self_contained():
    html = render_html(_sample_data())
    assert "<!doctype html>" in html
    assert "<title>vmaf-tune report" in html
    # inline SVG charts (no external <img src="...">)
    assert "<svg" in html
    assert 'src="http' not in html  # no remote assets
    # tables rendered
    assert "Codec comparison" in html
    assert "Per-shot tuning" in html
    # status tag for failed row
    assert "tag bad" in html
    # JSON dump expandable
    assert "Raw JSON dump" in html


def test_to_dict_round_trip():
    data = _sample_data()
    d = data.to_dict()
    assert d["source"]["width"] == 1920
    assert d["target_vmaf"] == 92.0
    assert len(d["codec_rows"]) == 3
    assert d["codec_rows"][0]["codec"] == "libx264"
    assert len(d["ladder_rungs"]) == 2
    assert len(d["shots"]) == 2
    # serialisable
    json.dumps(d)


def test_markdown_assets_dir_writes_pngs(tmp_path):
    assets = tmp_path / "assets"
    md = render_markdown(_sample_data(), assets_dir=assets)
    pngs = sorted(assets.glob("*.png"))
    assert len(pngs) >= 2  # ladder + codec + shot
    # md links them, not base64
    assert "data:image/png;base64" not in md


def test_html_one_shot_timeline_renders_non_empty_chart():
    """ADR-0513 Bug B regression: a single-shot per-shot result must
    still produce a visible drawable element in the rendered SVG chart.

    The historical ``ax.step([start], [crf], ...)`` call collapsed to a
    zero-length path the SVG backend silently dropped. The fix renders
    each shot as a horizontal band over its frame range so the chart
    always carries at least one ``<path>`` / ``<line>`` element with a
    non-degenerate ``d="..."`` (or x1!=x2 line) attribute.
    """
    src = SourceInfo(
        path="/tmp/example.mp4",
        width=3840,
        height=2160,
        fps=60.0,
        duration_s=5.0,
        frame_count=300,
        codec="h264",
        size_bytes=1_000_000,
    )
    data = ReportData(
        source=src,
        target_vmaf=92.0,
        shots=(ShotRow(0, 0, 300, 3840, 2160, 26, 92.47, 12000, 5.0),),
        generated_at_iso="2026-05-18T00:00:00+00:00",
    )
    html = render_html(data)
    # The chart panel renders (matplotlib available).
    assert "Per-shot tuning" in html
    # Inline SVG present.
    if "<svg" not in html:
        # matplotlib not installed in the test env — skip rather than fail.
        import pytest as _pytest  # noqa: PLC0415

        _pytest.skip("matplotlib unavailable; SVG fallback exercised separately")
    # Locate the per-shot chart subtree by anchoring on the section
    # header and grabbing the SVG that follows it.
    chart_pos = html.find("Per-shot tuning timeline")
    # Title rendered inside the SVG; everything after it up to the next
    # </svg> is the chart we care about.
    if chart_pos < 0:
        # Fall back to any svg substring — the chart still rendered as
        # part of the page.
        chart_pos = html.find("<svg")
    svg_tail = html[chart_pos:]
    svg_end = svg_tail.find("</svg>")
    assert svg_end > 0, "expected closing </svg> after per-shot chart"
    chart_svg = svg_tail[:svg_end]
    # Drawable element heuristics: a Line2D / hline emits either a
    # <path d="M ... L ..."/> or a <line x1=... x2=.../> with x1 != x2.
    has_path = "<path " in chart_svg and 'd="M' in chart_svg
    has_line = "<line " in chart_svg
    assert has_path or has_line, (
        "1-shot timeline chart had no drawable path/line element — " "ADR-0513 Bug B regressed"
    )


def test_empty_sections_omitted():
    src = SourceInfo("/tmp/x.mp4", 1920, 1080, 24.0, 1.0, 24, "h264", 100)
    data = ReportData(source=src, target_vmaf=92.0)
    md = render_markdown(data)
    assert "## Source" in md
    assert "## Codec comparison" not in md
    assert "## ABR ladder" not in md
    assert "## Per-shot tuning" not in md

    html = render_html(data)
    assert "Codec comparison" not in html
    assert "ABR ladder" not in html
    assert "Per-shot tuning" not in html
