#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

"""The reference scores of ``benchmark_netflix.py`` are the Netflix goldens.

The harness reads ``vmaf_v0.6.1`` through the ffmpeg filter, which is the
``vmaf`` CLI path. Its src01 reference once held the Python harness's older
76.66890519623612, so the CPU row always read ``DIFF``. The reference values are
read back from the golden assertions (never written to them) so they cannot
drift apart again.
"""

from __future__ import annotations

import re
from pathlib import Path

from testdata import benchmark_netflix

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = 5e-5  # the harness's own OK/DIFF threshold (places=4)


def _asserted(path: str, key: str) -> list[float]:
    text = (ROOT / "python" / "test" / path).read_text(encoding="utf-8")
    pattern = rf'results\[\d\]\["{key}"\], ([0-9.]+)'
    return [float(value) for value in re.findall(pattern, text)]


def test_src01_reference_is_the_cli_golden() -> None:
    goldens = _asserted("vmafexec_test.py", "VMAFEXEC_score")
    expected = benchmark_netflix.EXPECTED["src01_576x324"]
    assert any(abs(expected - golden) < TOLERANCE for golden in goldens), (expected, goldens)


def test_src01_reference_is_not_the_python_harness_value() -> None:
    assert abs(benchmark_netflix.EXPECTED["src01_576x324"] - 76.66890519623612) > 1e-3


def test_checkerboard_references_are_asserted_goldens() -> None:
    text = (ROOT / "python" / "test" / "quality_runner_test.py").read_text(encoding="utf-8")
    for name in ("checker_1080p_mild", "checker_1080p_heavy"):
        assert repr(benchmark_netflix.EXPECTED[name]) in text, name
