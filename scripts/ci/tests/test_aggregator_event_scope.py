#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""A master push aggregator judges the push, not the pull request on the same commit.

The merge train lands a pull request by fast-forward, so the pushed commit is
also the pull request's head, and the pull-request runs it cancels after
landing sit on that SHA. The aggregator read those cancelled runs of the
pull-request-only gates as failures of every master push
(T-CI-AGGREGATOR-READS-OTHER-EVENT-CHECKS-2026-10-06). These cases run the real
embedded script of .github/workflows/required-aggregator.yml.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from scripts.ci.required_aggregator_harness import run_required_aggregator

PR_ONLY_GATE = "Deliverables Checklist"
PUSH_SUITE = 1
PR_SUITE = 7
CODE_SCANNING_SUITE = 99
RUNS_ON_LANDED_COMMIT = (
    {"event": "push", "check_suite_id": PUSH_SUITE},
    {"event": "pull_request", "check_suite_id": PR_SUITE},
)


class AggregatorEventScope(unittest.TestCase):
    def test_push_ignores_cancelled_pull_request_runs(self) -> None:
        failures = run_required_aggregator(
            PR_ONLY_GATE,
            "cancelled",
            event="push",
            selected_suite=PR_SUITE,
            workflow_runs=RUNS_ON_LANDED_COMMIT,
        )
        self.assertEqual(failures, [])

    def test_push_still_fails_its_own_cancelled_run(self) -> None:
        failures = run_required_aggregator(
            PR_ONLY_GATE,
            "cancelled",
            event="push",
            selected_suite=PUSH_SUITE,
            workflow_runs=RUNS_ON_LANDED_COMMIT,
        )
        self.assertEqual(len(failures), 1)
        self.assertIn(f"{PR_ONLY_GATE}: cancelled", failures[0])

    def test_pull_request_still_reads_its_own_runs(self) -> None:
        failures = run_required_aggregator(
            PR_ONLY_GATE,
            "cancelled",
            event="pull_request",
            selected_suite=PR_SUITE,
            workflow_runs=RUNS_ON_LANDED_COMMIT,
        )
        self.assertEqual(len(failures), 1)
        self.assertIn(f"{PR_ONLY_GATE}: cancelled", failures[0])

    def test_push_still_reads_check_runs_of_other_apps(self) -> None:
        """Code scanning reports from its own suite, which no workflow run owns."""
        failures = run_required_aggregator(
            "CodeQL",
            "failure",
            event="push",
            selected_suite=CODE_SCANNING_SUITE,
            workflow_runs=RUNS_ON_LANDED_COMMIT,
        )
        self.assertEqual(len(failures), 1)
        self.assertIn("CodeQL: failure", failures[0])


if __name__ == "__main__":
    unittest.main()
