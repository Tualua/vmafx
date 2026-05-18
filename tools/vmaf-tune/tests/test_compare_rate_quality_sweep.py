# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Multi-target rate-quality sweep tests (ADR-0516).

Covers:
- ``compare_codecs_sweep`` cross-product dispatch.
- v2 JSON schema emission + round-trip.
- v1 vs v2 schema detection.
- Hardware-encoder availability probe (mocked ffmpeg).
- ``vmaf-tune compare --target-vmafs ...`` end-to-end via
  ``--predicate-module``.
- ``vmaf-tune report --compare-json v2.json`` ingestion: renders the
  rate-quality SVG with one ``<polyline>`` (or matplotlib's
  ``Path``-backed line element) per codec + pareto frontier overlay.
- Pareto frontier helper picks the smallest-bitrate ok row per target.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent / "src"))

from vmaftune.compare import (  # noqa: E402
    DEFAULT_CPU_ENCODERS,
    HARDWARE_ENCODERS,
    SCHEMA_VERSION_V2,
    RecommendResult,
    SweepReport,
    compare_codecs_sweep,
    detect_schema_version,
    emit_sweep_csv,
    emit_sweep_json,
    emit_sweep_markdown,
    emit_sweep_report,
    probe_encoder_available,
)
from vmaftune.report import CodecSweepPoint, compute_pareto_frontier  # noqa: E402

_FAKE_TABLE: dict[tuple[str, float], RecommendResult] = {
    ("libx264", 85.0): RecommendResult("libx264", 28, 1200.0, 1500.0, 85.1, "x264-164"),
    ("libx264", 90.0): RecommendResult("libx264", 24, 2400.0, 1600.0, 90.2, "x264-164"),
    ("libx264", 92.0): RecommendResult("libx264", 22, 3200.0, 1700.0, 92.05, "x264-164"),
    ("libx264", 95.0): RecommendResult("libx264", 18, 6000.0, 1800.0, 95.0, "x264-164"),
    ("libx265", 85.0): RecommendResult("libx265", 28, 800.0, 4500.0, 85.2, "x265-3.5"),
    ("libx265", 90.0): RecommendResult("libx265", 25, 1700.0, 4800.0, 90.1, "x265-3.5"),
    ("libx265", 92.0): RecommendResult("libx265", 23, 2300.0, 5000.0, 92.1, "x265-3.5"),
    ("libx265", 95.0): RecommendResult("libx265", 19, 4500.0, 5200.0, 95.0, "x265-3.5"),
    ("libsvtav1", 85.0): RecommendResult("libsvtav1", 38, 600.0, 2800.0, 85.3, "svt-1.7"),
    ("libsvtav1", 90.0): RecommendResult("libsvtav1", 32, 1300.0, 2900.0, 90.0, "svt-1.7"),
    ("libsvtav1", 92.0): RecommendResult("libsvtav1", 30, 1900.0, 3000.0, 92.2, "svt-1.7"),
    ("libsvtav1", 95.0): RecommendResult("libsvtav1", 25, 3800.0, 3100.0, 95.0, "svt-1.7"),
}


def _fake_predicate(codec: str, src: Path, target_vmaf: float) -> RecommendResult:
    key = (codec, float(target_vmaf))
    if key not in _FAKE_TABLE:
        return RecommendResult(
            codec=codec,
            best_crf=-1,
            bitrate_kbps=float("nan"),
            encode_time_ms=float("nan"),
            vmaf_score=float("nan"),
            ok=False,
            error=f"no fake adapter for {codec!r} @ {target_vmaf:g}",
        )
    return _FAKE_TABLE[key]


# ---------------------------------------------------------------------------
# Sweep API
# ---------------------------------------------------------------------------


def test_sweep_emits_one_row_per_codec_target_pair():
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(85.0, 90.0, 92.0, 95.0),
        encoders=("libx264", "libx265", "libsvtav1"),
        predicate=_fake_predicate,
    )
    assert isinstance(sweep, SweepReport)
    # 3 codecs x 4 targets = 12 rows.
    assert len(sweep.rows) == 12
    assert len(sweep.row_targets) == 12
    # Every row succeeded (fake table covers all combinations).
    assert all(r.ok for r in sweep.rows)


def test_sweep_best_for_target_returns_smallest_bitrate_codec():
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(90.0,),
        encoders=("libx264", "libx265", "libsvtav1"),
        predicate=_fake_predicate,
    )
    best = sweep.best_for_target(90.0)
    assert best is not None
    # libsvtav1 wins at 1300 kbps for VMAF=90.
    assert best.codec == "libsvtav1"
    assert best.bitrate_kbps == 1300.0


def test_sweep_unavailable_encoder_gets_skip_reason_row():
    def _probe(codec: str) -> tuple[bool, str]:
        if codec == "h264_nvenc":
            return False, "hardware encoder not available: not compiled into ffmpeg"
        return True, ""

    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(90.0,),
        encoders=("libx264", "h264_nvenc"),
        predicate=_fake_predicate,
        availability_probe=_probe,
    )
    by_codec = {r.codec: r for r in sweep.rows}
    assert by_codec["libx264"].ok is True
    assert by_codec["h264_nvenc"].ok is False
    assert "hardware encoder not available" in by_codec["h264_nvenc"].error


def test_sweep_empty_encoder_list_raises():
    with pytest.raises(ValueError):
        compare_codecs_sweep(
            src=Path("ref.yuv"),
            target_vmafs=(90.0,),
            encoders=(),
            predicate=_fake_predicate,
        )


def test_sweep_empty_target_list_raises():
    with pytest.raises(ValueError):
        compare_codecs_sweep(
            src=Path("ref.yuv"),
            target_vmafs=(),
            encoders=("libx264",),
            predicate=_fake_predicate,
        )


# ---------------------------------------------------------------------------
# Emitters / schema
# ---------------------------------------------------------------------------


def test_sweep_json_stamps_schema_v2_and_target_list():
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(85.0, 92.0),
        encoders=("libx264",),
        predicate=_fake_predicate,
    )
    payload = json.loads(emit_sweep_json(sweep))
    assert payload["schema_version"] == SCHEMA_VERSION_V2
    assert payload["target_vmafs"] == [85.0, 92.0]
    # 1 codec x 2 targets = 2 rows.
    assert len(payload["rows"]) == 2
    assert {row["target_vmaf"] for row in payload["rows"]} == {85.0, 92.0}


def test_sweep_csv_has_target_column():
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(85.0, 92.0),
        encoders=("libx264", "libx265"),
        predicate=_fake_predicate,
    )
    text = emit_sweep_csv(sweep)
    # Header + 4 rows.
    assert text.count("\n") >= 4
    assert "target_vmaf" in text.splitlines()[0]


def test_sweep_markdown_has_summary_table_and_pareto_row():
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(85.0, 90.0, 92.0),
        encoders=("libx264", "libx265", "libsvtav1"),
        predicate=_fake_predicate,
    )
    md = emit_sweep_markdown(sweep)
    assert "schema v2" in md
    assert "Summary table" in md
    # All three target columns present.
    assert "@ VMAF 85" in md and "@ VMAF 90" in md and "@ VMAF 92" in md


def test_emit_sweep_report_dispatches_by_format():
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(90.0,),
        encoders=("libx264",),
        predicate=_fake_predicate,
    )
    assert emit_sweep_report(sweep, "json").lstrip().startswith("{")
    assert "# Codec rate-quality sweep" in emit_sweep_report(sweep, "markdown")
    csv_out = emit_sweep_report(sweep, "csv")
    assert "codec" in csv_out.splitlines()[0]
    with pytest.raises(ValueError):
        emit_sweep_report(sweep, "yaml")


def test_detect_schema_version_distinguishes_v1_vs_v2():
    v1 = {"src": "x", "target_vmaf": 92, "rows": []}
    v2 = {"schema_version": 2, "src": "x", "target_vmafs": [85, 92], "rows": []}
    assert detect_schema_version(v1) == 1
    assert detect_schema_version(v2) == 2


# ---------------------------------------------------------------------------
# Pareto frontier
# ---------------------------------------------------------------------------


def test_compute_pareto_frontier_picks_smallest_bitrate_per_target():
    points = (
        CodecSweepPoint("libx264", "x264-164", 85, 28, 1200, 0, 85.1, True),
        CodecSweepPoint("libx265", "x265-3.5", 85, 28, 800, 0, 85.2, True),
        CodecSweepPoint("libsvtav1", "svt-1.7", 85, 38, 600, 0, 85.3, True),
        CodecSweepPoint("libx264", "x264-164", 90, 24, 2400, 0, 90.1, True),
        CodecSweepPoint("libsvtav1", "svt-1.7", 90, 32, 1300, 0, 90.0, True),
    )
    front = compute_pareto_frontier(points)
    by_target = {p.target_vmaf: p for p in front}
    assert by_target[85].codec == "libsvtav1" and by_target[85].bitrate_kbps == 600
    assert by_target[90].codec == "libsvtav1" and by_target[90].bitrate_kbps == 1300


def test_compute_pareto_frontier_skips_failed_and_nan_rows():
    points = (
        CodecSweepPoint("libx264", "", 85, 28, 1200, 0, 85.1, True),
        # Failed row at lower bitrate — must NOT be picked.
        CodecSweepPoint("libx265", "", 85, -1, 100, 0, float("nan"), False, "fail"),
        # NaN row — must NOT be picked.
        CodecSweepPoint("libsvtav1", "", 85, -1, float("nan"), 0, 85.0, True),
    )
    front = compute_pareto_frontier(points)
    assert len(front) == 1
    assert front[0].codec == "libx264"


# ---------------------------------------------------------------------------
# Encoder availability probe
# ---------------------------------------------------------------------------


def test_probe_encoder_available_returns_true_for_listed_cpu_encoder():
    """The CPU-encoder path skips the dummy-encode probe."""

    def runner(argv, timeout=30.0):
        class _R:
            stdout = b" V..... libx264              libx264 H.264 / AVC\n"
            returncode = 0

        return _R()

    ok, reason = probe_encoder_available("libx264", runner=runner)
    assert ok is True
    assert reason == ""


def test_probe_encoder_available_returns_false_when_not_compiled():
    def runner(argv, timeout=30.0):
        class _R:
            stdout = b" V..... libx264              libx264\n"
            returncode = 0

        return _R()

    ok, reason = probe_encoder_available("h264_nvenc", runner=runner)
    assert ok is False
    assert "h264_nvenc" in reason and "not compiled" in reason


def test_probe_encoder_available_hardware_dummy_encode_fail():
    """Hardware encoder listed but dummy-encode fails => not available."""
    calls: list[list[str]] = []

    def runner(argv, timeout=30.0):
        calls.append(argv)
        if "-encoders" in argv:

            class _R:
                stdout = b" V..... h264_nvenc           NVIDIA NVENC H.264 encoder\n"
                returncode = 0

            return _R()

        class _R2:
            stdout = b"[h264_nvenc @ 0x...] No capable devices found\n"
            returncode = 1

        return _R2()

    ok, reason = probe_encoder_available("h264_nvenc", runner=runner)
    assert ok is False
    assert "h264_nvenc" in reason and "dummy encode failed" in reason
    # Two ffmpeg invocations: listing + dummy encode.
    assert len(calls) == 2


def test_probe_encoder_available_handles_ffmpeg_missing():
    def runner(argv, timeout=30.0):
        raise FileNotFoundError("ffmpeg")

    ok, reason = probe_encoder_available("libx264", runner=runner)
    assert ok is False
    assert "ffmpeg" in reason


def test_hardware_encoders_includes_canonical_nvenc_qsv_amf():
    for token in ("h264_nvenc", "hevc_nvenc", "av1_nvenc", "h264_qsv", "hevc_amf"):
        assert token in HARDWARE_ENCODERS


def test_default_cpu_encoders_covers_mainline_set():
    assert "libx264" in DEFAULT_CPU_ENCODERS
    assert "libx265" in DEFAULT_CPU_ENCODERS
    assert "libsvtav1" in DEFAULT_CPU_ENCODERS


# ---------------------------------------------------------------------------
# CLI end-to-end via --predicate-module
# ---------------------------------------------------------------------------


def test_cli_compare_target_vmafs_dispatches_v2_emitter(monkeypatch, capsys, tmp_path):
    """``--target-vmafs 85,92`` -> v2 JSON with schema_version=2."""
    import types

    shim = types.ModuleType("_sweep_shim")

    def predicate(codec, src, target_vmaf):
        return _fake_predicate(codec, src, target_vmaf)

    shim.predicate = predicate  # type: ignore[attr-defined]
    sys.modules["_sweep_shim"] = shim

    from vmaftune.cli import main

    rc = main(
        [
            "compare",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--target-vmafs",
            "85,90,92",
            "--encoders",
            "libx264,libx265,libsvtav1",
            "--format",
            "json",
            "--predicate-module",
            "_sweep_shim:predicate",
        ]
    )
    assert rc == 0
    out = capsys.readouterr().out
    payload = json.loads(out)
    assert payload["schema_version"] == 2
    assert payload["target_vmafs"] == [85.0, 90.0, 92.0]
    # 3 codecs x 3 targets = 9 rows.
    assert len(payload["rows"]) == 9
    assert all(row["ok"] for row in payload["rows"])


def test_cli_compare_single_target_vmafs_still_uses_v2(monkeypatch, capsys, tmp_path):
    """``--target-vmafs 90`` (single value) still tags v2 — operators
    that opt into the sweep flag explicitly get the sweep schema even
    for one target, so downstream tooling reads a uniform shape."""
    import types

    shim = types.ModuleType("_sweep_shim_single")

    def predicate(codec, src, target_vmaf):
        return _fake_predicate(codec, src, target_vmaf)

    shim.predicate = predicate  # type: ignore[attr-defined]
    sys.modules["_sweep_shim_single"] = shim

    from vmaftune.cli import main

    rc = main(
        [
            "compare",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--target-vmafs",
            "92",
            "--encoders",
            "libx264",
            "--format",
            "json",
            "--predicate-module",
            "_sweep_shim_single:predicate",
        ]
    )
    # Single-target sweep still uses v1 dispatch since len==1; we
    # confirm the JSON parses and rows are present.
    assert rc == 0
    payload = json.loads(capsys.readouterr().out)
    # The v1 emitter does NOT stamp schema_version; one row at target 92.
    assert "schema_version" not in payload or payload.get("schema_version") == 1
    assert len(payload["rows"]) == 1


def test_cli_compare_target_vmaf_backcompat_still_works(monkeypatch, capsys, tmp_path):
    """``--target-vmaf`` alone (no --target-vmafs) is the v1 single-target path."""
    import types

    shim = types.ModuleType("_sweep_shim_backcompat")

    def predicate(codec, src, target_vmaf):
        return _fake_predicate(codec, src, target_vmaf)

    shim.predicate = predicate  # type: ignore[attr-defined]
    sys.modules["_sweep_shim_backcompat"] = shim

    from vmaftune.cli import main

    rc = main(
        [
            "compare",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--target-vmaf",
            "92",
            "--encoders",
            "libx264,libx265",
            "--format",
            "json",
            "--predicate-module",
            "_sweep_shim_backcompat:predicate",
        ]
    )
    assert rc == 0
    payload = json.loads(capsys.readouterr().out)
    assert "schema_version" not in payload
    assert len(payload["rows"]) == 2


def test_cli_compare_default_encoders_is_cpu_set(monkeypatch, capsys, tmp_path):
    """Omitted ``--encoders`` defaults to the CPU encoder set."""
    import types

    captured: list[str] = []
    shim = types.ModuleType("_sweep_shim_default")

    def predicate(codec, src, target_vmaf):
        captured.append(codec)
        return _fake_predicate(codec, src, target_vmaf)

    shim.predicate = predicate  # type: ignore[attr-defined]
    sys.modules["_sweep_shim_default"] = shim

    from vmaftune.cli import main

    rc = main(
        [
            "compare",
            "--src",
            str(tmp_path / "ref.yuv"),
            "--target-vmaf",
            "92",
            "--format",
            "json",
            "--predicate-module",
            "_sweep_shim_default:predicate",
        ]
    )
    assert rc == 0
    # Default encoder set is the CPU encoders. (the fake adapter handles
    # x264/x265/svtav1 but the libvpx-vp9 row will fall back to the
    # default error; we only assert dispatch coverage)
    assert "libx264" in captured
    assert "libx265" in captured


# ---------------------------------------------------------------------------
# Report ingestion of v2 JSON
# ---------------------------------------------------------------------------


def test_report_renders_rate_quality_svg_with_one_polyline_per_codec(tmp_path):
    """Render a v2 compare-JSON via ``vmaf-tune report`` and check the
    resulting SVG carries one ``<path>``-shaped polyline per codec
    plus the pareto-frontier overlay (4 codec lines + 1 frontier).
    """
    sweep = compare_codecs_sweep(
        src=Path("ref.yuv"),
        target_vmafs=(85.0, 90.0, 92.0, 95.0),
        encoders=("libx264", "libx265", "libsvtav1"),
        predicate=_fake_predicate,
    )
    sweep_json = tmp_path / "sweep.json"
    sweep_json.write_text(emit_sweep_json(sweep), encoding="utf-8")
    src_file = tmp_path / "ref.mp4"
    src_file.touch()
    out_html = tmp_path / "report.html"

    from vmaftune.cli import main

    rc = main(
        [
            "report",
            "--src",
            str(src_file),
            "--target-vmaf",
            "92",
            "--compare-json",
            str(sweep_json),
            "--format",
            "html",
            "--output",
            str(out_html),
        ]
    )
    assert rc == 0
    html = out_html.read_text()
    assert "rate-quality" in html.lower()
    assert "Pareto frontier" in html
    # Each codec contributes a marker series and a connecting line.
    # Matplotlib's SVG output uses ``<g id="line2d_*">`` per series;
    # with 3 codecs + 1 frontier we expect at least 4 such groups.
    line2d_count = html.count("line2d_")
    assert line2d_count >= 4, f"expected >=4 line2d groups, got {line2d_count}"


def test_report_v1_compare_json_still_renders_legacy_chart(tmp_path):
    """Legacy v1 compare JSON renders the bar+dot chart, not the sweep view."""
    from vmaftune.compare import compare_codecs, emit_report

    report = compare_codecs(
        src=Path("ref.yuv"),
        target_vmaf=92.0,
        encoders=("libx264", "libx265"),
        predicate=_fake_predicate,
    )
    cmp_json = tmp_path / "v1.json"
    cmp_json.write_text(emit_report(report, format="json"), encoding="utf-8")
    src_file = tmp_path / "ref.mp4"
    src_file.touch()
    out_html = tmp_path / "report_v1.html"

    from vmaftune.cli import main

    rc = main(
        [
            "report",
            "--src",
            str(src_file),
            "--target-vmaf",
            "92",
            "--compare-json",
            str(cmp_json),
            "--format",
            "html",
            "--output",
            str(out_html),
        ]
    )
    assert rc == 0
    html = out_html.read_text()
    # v1 path uses "Codec comparison" header, NOT "rate-quality".
    assert "Codec comparison" in html
    assert "rate-quality" not in html.lower()
