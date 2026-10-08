# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

"""`Go API Compatibility` is a required aggregator check that cannot wait forever (ADR-1506)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NAME = "Go API Compatibility"


def _list(text: str, start: str) -> set[str]:
    body = re.search(rf"const {start} = \[(.*?)\];", text, re.DOTALL)
    assert body is not None
    return set(re.findall(r"'([^']+)'", re.sub(r"(?m)^\s*//.*$", "", body.group(1))))


class GoApiCompatRequiredTests(unittest.TestCase):
    def setUp(self) -> None:
        self.aggregator = (ROOT / ".github/workflows/required-aggregator.yml").read_text()
        self.workflow = (ROOT / ".github/workflows/praetor-api.yml").read_text()

    def test_required_and_strict(self) -> None:
        self.assertIn(NAME, _list(self.aggregator, "required"))
        # The workflow listens for ready_for_review since praetor#815, so a draft that becomes ready
        # is re-run and a missing result is a broken workflow, never a skip (ADR-2440).
        self.assertIn(NAME, _list(self.aggregator, "strictMustReport"))

    def test_workflow_reports_on_every_pull_request(self) -> None:
        self.assertIn(f"name: {NAME}", self.workflow)
        self.assertIsNone(re.search(r"(?m)^\s*paths(-ignore)?:", self.workflow))
        # A job-level condition would skip the required context; the draft stop is a step (praetor#815).
        self.assertIsNone(re.search(r"(?m)^ {4}if:", self.workflow))
        self.assertIn("ready_for_review", self.workflow)


if __name__ == "__main__":
    unittest.main()
