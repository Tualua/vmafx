#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Contract tests for scripts/test/zerocopy_throughput_report.py (pure Python)."""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from typing import Any

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.test import zerocopy_throughput_report as rep

FRAMES = 200
EXIT_SPREAD = 3
MEDIAN_FREQ_MHZ = 1200
FDINFO = "drm-driver:\ti915\ndrm-engine-render:\t{r} ns\ndrm-engine-copy:\t{c} ns\n"


def _write_run(
    d: Path, name: str = "run", rtime: str = "4.000", rc: int = 0, fdinfo: bool = True
) -> Path:
    base = d / name
    frames = [{"frameNum": i, "metrics": {"vmaf": 90.0}} for i in range(FRAMES)]
    doc = {"frames": frames, "pooled_metrics": {"vmaf": {"mean": 91.5}}}
    Path(f"{base}.json").write_text(json.dumps(doc))
    Path(f"{base}.err").write_text(
        "[vmaf-sycl] timing: 200 frames, avg cpu=1.50ms gpu=18.25ms total=19.75ms (50.6 fps)\n"
        "[vmaf-sycl] phases: 200 frames, queue_wait=0.500ms combined_wait=1.250ms "
        "graph_wait=0.000ms import=2.000ms (avg host ms per frame)\n"
        f"bench: utime=1s rtime={rtime}s\n"
    )
    Path(f"{base}.time").write_text("TIME real=4.000 user=2.000 sys=1.000\n")
    Path(f"{base}.rc").write_text(f"{rc}\n")
    if fdinfo:
        Path(f"{base}.fdinfo.first").write_text(FDINFO.format(r=1000, c=0))
        Path(f"{base}.fdinfo.last").write_text(FDINFO.format(r=2_001_000_000, c=500))
        Path(f"{base}.freq").write_text("300\n1200\n2000\n")
    return base


def test_parse_run_values(tmp_path: Path) -> None:
    base = _write_run(tmp_path)
    row = rep.parse_run(base, {"label": "x", "frames": "200"})
    assert row["fps"] == pytest.approx(50.0)
    assert row["sycl_avg_gpu_ms"] == pytest.approx(18.25)
    assert row["phases"]["import"] == pytest.approx(2.0)
    assert row["host_cpu_ms_per_frame"] == pytest.approx(15.0)
    assert row["engine_ns"]["drm-engine-render"] == 2_001_000_000 - 1000
    assert row["engine_busy_pct"]["drm-engine-render"] == pytest.approx(50.025, rel=1e-3)
    assert row["freq_mhz"]["median"] == MEDIAN_FREQ_MHZ
    assert row["vmaf_mean"] == pytest.approx(91.5)
    assert row["rc"] == 0


def test_engines_sum_over_clients(tmp_path: Path) -> None:
    path = tmp_path / "fdinfo"
    path.write_text(FDINFO.format(r=10, c=1) + "--\n" + FDINFO.format(r=5, c=7))
    assert rep._engines(path) == {"drm-engine-render": 15, "drm-engine-copy": 8}


def test_parse_run_without_fdinfo(tmp_path: Path) -> None:
    row = rep.parse_run(_write_run(tmp_path, fdinfo=False), {})
    assert "engine_ns" not in row
    assert "freq_mhz" not in row
    assert row["fps"] == pytest.approx(50.0)


def test_main_appends_jsonl(tmp_path: Path) -> None:
    base = _write_run(tmp_path)
    assert rep.main(["parse-run", "--base", str(base), "--meta", "label=a", "repeat=1"]) == 0
    lines = (tmp_path / "results.jsonl").read_text().splitlines()
    assert len(lines) == 1
    assert json.loads(lines[0])["label"] == "a"


def _rows(fps: list[float], rc: int = 0) -> list[dict[str, Any]]:
    return [{"label": "g", "ladder": "L2", "fps": f, "rc": rc} for f in fps]


def test_summarize_spread_invalid(capsys: pytest.CaptureFixture[str]) -> None:
    assert rep.summarize(_rows([40.0, 50.0, 45.0]), 0.05) == EXIT_SPREAD
    assert "INVALID-SPREAD" in capsys.readouterr().out


def test_summarize_spread_ok(capsys: pytest.CaptureFixture[str]) -> None:
    assert rep.summarize(_rows([45.0, 45.4, 45.2]), 0.05) == 0
    assert "INVALID-SPREAD" not in capsys.readouterr().out


def test_summarize_failed_run(capsys: pytest.CaptureFixture[str]) -> None:
    assert rep.summarize(_rows([45.0], rc=1), 0.05) == 1
    assert "FAILED-RUN" in capsys.readouterr().out


def _log(path: Path, vif: float, n: int = 3) -> Path:
    frames = [{"frameNum": i, "metrics": {"integer_vif_scale0": vif}} for i in range(n)]
    path.write_text(json.dumps({"frames": frames, "pooled_metrics": {}}))
    return path


def test_same_scores_identical(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a, b = _log(tmp_path / "a.json", 0.5), _log(tmp_path / "b.json", 0.5)
    assert rep.main(["same-scores", str(a), str(b)]) == 0
    assert "IDENTICAL frames=3" in capsys.readouterr().out


def test_same_scores_one_ulp(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a = _log(tmp_path / "a.json", 0.5)
    b = _log(tmp_path / "b.json", math.nextafter(0.5, 1.0))
    assert rep.main(["same-scores", str(a), str(b)]) == 1
    assert "DIFF" in capsys.readouterr().out


def test_same_scores_frame_count(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a, b = _log(tmp_path / "a.json", 0.5, 3), _log(tmp_path / "b.json", 0.5, 4)
    assert rep.main(["same-scores", str(a), str(b)]) == 1
    assert "frame count" in capsys.readouterr().out
