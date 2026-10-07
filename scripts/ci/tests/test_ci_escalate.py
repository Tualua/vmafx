#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Positive, negative and boundary cases for scripts/ci/ci_escalate.py (ADR-2169)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from typing import ClassVar

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from scripts.ci import ci_escalate, ci_tier

REPO = "VMAFx/vmafx"
SHA = "a" * 40


class FakeApi:
    """A recording stand-in for the Actions API."""

    def __init__(self, runs: list[dict[str, object]], completes_after: int = 1) -> None:
        self.runs = runs
        self.calls: list[tuple[str, str]] = []
        self.polls = 0
        self.completes_after = completes_after

    def __call__(self, method: str, path: str) -> object:
        self.calls.append((method, path))
        if path.startswith(f"/repos/{REPO}/actions/runs?"):
            return {"workflow_runs": self.runs}
        if method == "GET" and "/actions/runs/" in path:
            self.polls += 1
            done = self.polls >= self.completes_after
            return {"status": "completed" if done else "in_progress"}
        return {}


def run(run_id: int, workflow_id: int, **extra: object) -> dict[str, object]:
    return {
        "id": run_id,
        "workflow_id": workflow_id,
        "name": f"wf{workflow_id}",
        "status": "completed",
        "path": f".github/workflows/wf{workflow_id}.yml",
        **extra,
    }


class LatestRuns(unittest.TestCase):
    def test_keeps_the_newest_run_of_each_workflow(self) -> None:
        api = FakeApi([run(1, 10), run(5, 10), run(3, 11)])
        picked = ci_escalate.latest_runs(api, REPO, SHA, "999")
        self.assertEqual(sorted(r["id"] for r in picked), [3, 5])

    def test_leaves_out_this_run_and_the_escalation_workflow(self) -> None:
        api = FakeApi(
            [run(7, 10), run(8, 11, path=".github/workflows/ci-escalate.yml"), run(2, 12)]
        )
        picked = ci_escalate.latest_runs(api, REPO, SHA, "7")
        self.assertEqual([r["id"] for r in picked], [2])

    def test_pages_are_bounded(self) -> None:
        full_page = [run(i, i) for i in range(1, ci_tier.PAGE_SIZE + 1)]
        api = FakeApi(full_page)
        ci_escalate.latest_runs(api, REPO, SHA, "0")
        listings = [c for c in api.calls if "/actions/runs?" in c[1]]
        self.assertEqual(len(listings), ci_tier.MAX_PAGES)


class Rerun(unittest.TestCase):
    def test_a_completed_run_is_re_run_without_a_cancel(self) -> None:
        api = FakeApi([])
        message = ci_escalate.rerun(api, REPO, run(4, 1), sleep=lambda _s: None)
        self.assertIn("re-run", message)
        self.assertEqual([c[0] for c in api.calls], ["POST"])
        self.assertTrue(api.calls[0][1].endswith("/runs/4/rerun"))

    def test_a_running_run_is_cancelled_awaited_then_re_run(self) -> None:
        api = FakeApi([], completes_after=3)
        message = ci_escalate.rerun(
            api, REPO, run(4, 1, status="in_progress"), sleep=lambda _s: None
        )
        self.assertIn("re-run", message)
        verbs = [(c[0], c[1].rsplit("/", 1)[1]) for c in api.calls]
        self.assertEqual(verbs[0], ("POST", "cancel"))
        self.assertEqual(verbs[-1], ("POST", "rerun"))
        self.assertEqual(api.polls, 3)

    def test_a_run_that_never_stops_is_reported_not_re_run(self) -> None:
        api = FakeApi([], completes_after=10**6)
        message = ci_escalate.rerun(
            api, REPO, run(4, 1, status="in_progress"), sleep=lambda _s: None
        )
        self.assertIn("not re-run", message)
        self.assertFalse(any(c[1].endswith("/rerun") for c in api.calls))
        self.assertEqual(api.polls, ci_escalate.CANCEL_WAIT_S // ci_escalate.CANCEL_POLL_S)


class OwesFullSuite(unittest.TestCase):
    ENV: ClassVar[dict[str, str]] = {
        "EVENT_NAME": "pull_request",
        "GITHUB_REPOSITORY": REPO,
        "HEAD_REPOSITORY": REPO,
        "HEAD_REF": "fix/example",
        "PR_NUMBER": "7",
        "PR_LABELS": "[]",
    }

    def test_the_full_label_escalates(self) -> None:
        fetch = lambda path: [{"name": "ci: full"}]  # noqa: E731
        self.assertTrue(ci_escalate.owes_full_suite(self.ENV, fetch, ci_tier.DEFAULT_CONFIG))

    def test_an_unrelated_label_does_not(self) -> None:
        fetch = lambda path: [{"name": "dependencies"}]  # noqa: E731
        self.assertFalse(ci_escalate.owes_full_suite(self.ENV, fetch, ci_tier.DEFAULT_CONFIG))

    def test_a_cut_label_on_an_ordinary_pull_request_does_not(self) -> None:
        fetch = lambda path: [{"name": "autorelease: cut"}]  # noqa: E731
        self.assertFalse(ci_escalate.owes_full_suite(self.ENV, fetch, ci_tier.DEFAULT_CONFIG))


if __name__ == "__main__":
    unittest.main()
