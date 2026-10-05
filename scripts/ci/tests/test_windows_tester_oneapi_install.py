#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The Windows SYCL leg's oneAPI install step survives a locked installer (ADR-1566).

Run 37271658228 failed in this step: `Remove-Item $exe` right after the silent
self-extraction raised "being used by another process" and the leg ended. The step
must check the extractor's exit code, find `bootstrapper.exe`, and remove the 2.5 GB
installer with a bounded retry (HISS-02) that warns, never throws, when the file stays
locked. A hosted Windows run is the only full proof; this keeps the shape from
regressing without one.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path
from typing import Any

import yaml  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = ROOT / ".github" / "workflows" / "windows-tester-bundle.yml"


def install_script() -> str:
    data: dict[str, Any] = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    for job in data["jobs"].values():
        for step in job.get("steps", []):
            if str(step.get("name", "")).startswith("Install Intel oneAPI"):
                return str(step["run"])
    raise AssertionError("no 'Install Intel oneAPI' step in windows-tester-bundle.yml")


class OneApiInstallStep(unittest.TestCase):
    def test_extractor_exit_code_is_checked(self) -> None:
        script = install_script()
        self.assertRegex(script, r"\$x = Start-Process -FilePath \$exe .*-PassThru")
        self.assertIn("$x.ExitCode -ne 0", script)

    def test_bootstrapper_is_required_after_extraction(self) -> None:
        self.assertRegex(install_script(), r"Test-Path \$boot")

    def test_installer_removal_is_never_a_bare_remove_item(self) -> None:
        script = install_script()
        # The failing form: a removal of the installer outside a try block.
        self.assertNotRegex(script, r"(?m)^\s*Remove-Item \$exe\s*$")
        self.assertIsNotNone(
            re.search(r"try \{ Remove-Item -LiteralPath \$exe -ErrorAction Stop", script)
        )

    def test_removal_retry_is_bounded_and_warns(self) -> None:
        script = install_script()
        self.assertRegex(script, r"\$try -le \d+")
        self.assertIn("##[warning]", script)


if __name__ == "__main__":
    unittest.main()
