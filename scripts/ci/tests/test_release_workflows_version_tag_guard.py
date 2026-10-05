#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every `on: release` workflow must ignore the tester prereleases.

`macos-tester-bundle.yml` and `windows-tester-bundle.yml` publish prereleases
named `tester-*`. A published release fires the `release` event, so a workflow
that reacts to it must start its jobs for version tags (`v*`) only; otherwise
the production publish, operator-node and supply-chain workflows run on a tester
tag, fail at "Validate tag" and turn master red. The guard sits on the first job
(`validate-release`), every other job depends on it and skips with it, and a
summary job that runs under `always()` carries the same guard.
"""

from __future__ import annotations

import unittest
from pathlib import Path
from typing import Any

import yaml  # type: ignore[import-untyped]

REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOWS = REPO_ROOT / ".github" / "workflows"
GUARD = "github.event_name != 'release' || startsWith(github.event.release.tag_name, 'v')"


def _triggers(doc: dict[Any, Any]) -> dict[str, Any]:
    # PyYAML reads the bare key `on` as boolean True.
    raw = doc.get("on", doc.get(True)) or {}
    return raw if isinstance(raw, dict) else dict.fromkeys([raw] if isinstance(raw, str) else raw)


def release_workflows() -> dict[str, dict[str, Any]]:
    found: dict[str, dict[str, Any]] = {}
    for path in sorted(WORKFLOWS.glob("*.yml")):
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        if isinstance(doc, dict) and "release" in _triggers(doc):
            found[path.name] = doc
    return found


def _reaches(jobs: dict[str, Any], name: str, root: str) -> bool:
    """True when job `name` is `root` or (transitively) needs it."""
    pending, seen = [name], set()
    while pending:
        job = pending.pop()
        if job == root:
            return True
        if job in seen:
            continue
        seen.add(job)
        needs = jobs[job].get("needs") or []
        pending.extend([needs] if isinstance(needs, str) else needs)
    return False


class ReleaseWorkflowGuard(unittest.TestCase):
    def test_release_workflows_exist(self) -> None:
        self.assertTrue(release_workflows(), "no `on: release` workflow found: the scan is broken")

    def test_first_job_carries_version_tag_guard(self) -> None:
        for name, doc in release_workflows().items():
            with self.subTest(workflow=name):
                cond = " ".join(str(doc["jobs"]["validate-release"].get("if", "")).split())
                self.assertIn(GUARD, cond)

    def test_every_job_skips_with_the_guard(self) -> None:
        for name, doc in release_workflows().items():
            jobs = doc["jobs"]
            for job, body in jobs.items():
                with self.subTest(workflow=name, job=job):
                    guarded = _reaches(jobs, job, "validate-release")
                    own = GUARD in " ".join(str(body.get("if", "")).split())
                    self.assertTrue(guarded or own, f"{job} runs on a tester release")
                    if "always()" in str(body.get("if", "")):
                        self.assertTrue(own, f"{job} runs under always() without the guard")


if __name__ == "__main__":
    unittest.main()
