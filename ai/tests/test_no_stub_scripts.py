# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The two removed PTQ stub scripts do not come back.

``gen_calibration.py`` and ``quantize_int8.py`` printed "not yet implemented"
and exited 1 while ``vmaf-train quantize-int8`` already did the work (static
PTQ calibrated from a parquet cache). A stub that looks like an entry point
sends a reader to a command that cannot run, so they are removed and this
test keeps them removed.
"""

from __future__ import annotations

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = REPO_ROOT / "ai" / "scripts"
REMOVED = ("gen_calibration.py", "quantize_int8.py", "build_calibration_set.py")


def test_scripts_directory_is_populated() -> None:
    assert len(list(SCRIPTS.glob("*.py"))) > 20


def test_removed_stubs_stay_removed() -> None:
    assert [n for n in REMOVED if (SCRIPTS / n).exists()] == []


def test_quantize_entry_point_is_the_cli_command() -> None:
    cli = (REPO_ROOT / "ai" / "src" / "vmaf_train" / "cli.py").read_text()
    assert "def quantize_int8_cmd" in cli
