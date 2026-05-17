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
