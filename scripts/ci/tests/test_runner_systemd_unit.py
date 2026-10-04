#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The self-hosted runner unit must point at a supervisor that exists in the tree.

The unit named `%h/dev/vmaf/...`, the path of the archived repository, so a
host that followed the install steps started nothing. The unit assumes the
clone at `%h/dev/vmafx/vmafx`; this test maps that prefix onto the checkout
under test and requires every path the unit names to exist there.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UNIT = ROOT / "dev" / "systemd" / "vmafx-sycl-arc-runner.service"
PREFIX = "%h/dev/vmafx/vmafx"


def _value(key: str) -> str:
    for line in UNIT.read_text(encoding="utf-8").splitlines():
        m = re.match(rf"{key}=(\S+)", line)
        if m:
            return m.group(1)
    raise AssertionError(f"{key}= missing in {UNIT}")


class RunnerUnit(unittest.TestCase):
    def test_paths_use_the_vmafx_clone_prefix(self) -> None:
        for key in ("WorkingDirectory", "ExecStart"):
            self.assertTrue(_value(key).startswith(PREFIX), f"{key}= has a stale prefix")

    def test_working_directory_is_the_repo_root(self) -> None:
        self.assertEqual(_value("WorkingDirectory"), PREFIX)

    def test_exec_start_names_an_existing_executable(self) -> None:
        rel = _value("ExecStart")[len(PREFIX) + 1 :]
        path = ROOT / rel
        self.assertTrue(path.is_file(), f"{rel} is not in the tree")
        self.assertTrue(path.stat().st_mode & 0o111, f"{rel} is not executable")


if __name__ == "__main__":
    unittest.main()
