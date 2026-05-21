# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Tests for ``ai/scripts/materialize_saliency_features.py``."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest

np = pytest.importorskip("numpy")

_REPO_ROOT = Path(__file__).resolve().parents[2]
_SCRIPT_PATH = _REPO_ROOT / "ai" / "scripts" / "materialize_saliency_features.py"


def _load_module():
    spec = importlib.util.spec_from_file_location("materialize_saliency_features", _SCRIPT_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _runner(cmd: list[str], **_: Any) -> subprocess.CompletedProcess[str]:
    if cmd[0] == "ffprobe-test":
        payload = {"streams": [{"width": 4, "height": 4}]}
        return subprocess.CompletedProcess(cmd, 0, stdout=json.dumps(payload), stderr="")
    if cmd[0] == "ffmpeg-test":
        Path(cmd[-1]).write_bytes(b"\x10" * (4 * 4 * 3 // 2))
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")
    raise AssertionError(f"unexpected command: {cmd}")


def _saliency_fn(
    raw_path: Path,
    width: int,
    height: int,
    *,
    model_path: Path | None = None,
    frame_samples: int = 8,
) -> np.ndarray:
    assert raw_path.is_file()
    assert (width, height) == (4, 4)
    assert model_path is None
    assert frame_samples == 1
    return np.asarray([[0.0, 0.5], [1.0, 0.5]], dtype=np.float32)


def test_read_write_jsonl_roundtrip(tmp_path: Path) -> None:
    module = _load_module()
    rows = [{"src": "a.mp4", "width": 4}, {"src": "b.mp4", "width": 8}]
    path = tmp_path / "features.jsonl"

    module.write_table(path, rows)

    assert module.read_table(path) == rows


def test_materialize_rows_decodes_and_writes_aggregates(tmp_path: Path) -> None:
    module = _load_module()
    source = tmp_path / "clip.mp4"
    source.write_bytes(b"not a real mp4")
    cfg = module.SaliencyMaterializeConfig(
        ffmpeg_bin="ffmpeg-test",
        ffprobe_bin="ffprobe-test",
        max_frames=1,
        frame_samples=3,
    )

    enriched, summary = module.materialize_rows(
        [{"src": str(source), "width": 4, "height": 4}],
        cfg,
        runner=_runner,
        saliency_fn=_saliency_fn,
    )

    assert summary == module.MaterializeSummary(total=1, ok=1, skipped_existing=0, failed=0)
    assert enriched[0]["saliency_status"] == "ok"
    assert enriched[0]["saliency_mean"] == pytest.approx(0.5)
    assert enriched[0]["saliency_var"] == pytest.approx(0.125)


def test_materialize_rows_probes_missing_geometry(tmp_path: Path) -> None:
    module = _load_module()
    source = tmp_path / "clip.mp4"
    source.write_bytes(b"not a real mp4")
    cfg = module.SaliencyMaterializeConfig(
        ffmpeg_bin="ffmpeg-test",
        ffprobe_bin="ffprobe-test",
        max_frames=1,
        frame_samples=1,
    )

    enriched, summary = module.materialize_rows(
        [{"src": str(source)}],
        cfg,
        runner=_runner,
        saliency_fn=_saliency_fn,
    )

    assert summary.ok == 1
    assert enriched[0]["saliency_status"] == "ok"


def test_materialize_rows_skips_existing_saliency() -> None:
    module = _load_module()
    cfg = module.SaliencyMaterializeConfig()

    enriched, summary = module.materialize_rows(
        [{"src": "missing.mp4", "saliency_mean": 0.2, "saliency_var": 0.01}],
        cfg,
        runner=_runner,
        saliency_fn=_saliency_fn,
    )

    assert summary == module.MaterializeSummary(total=1, ok=0, skipped_existing=1, failed=0)
    assert enriched[0]["saliency_mean"] == pytest.approx(0.2)
    assert enriched[0]["saliency_var"] == pytest.approx(0.01)
    assert enriched[0]["saliency_status"] == "skipped-existing"


def test_materialize_rows_marks_missing_source(tmp_path: Path) -> None:
    module = _load_module()
    cfg = module.SaliencyMaterializeConfig(root=tmp_path)

    enriched, summary = module.materialize_rows(
        [{"src": "missing.mp4"}],
        cfg,
        runner=_runner,
        saliency_fn=_saliency_fn,
    )

    assert summary.failed == 1
    assert enriched[0]["saliency_status"] == "missing-source"
