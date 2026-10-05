# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Planted-case tests for the inherited-tag deletion script and the push guard (ADR-1805)."""

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path
from types import ModuleType
from typing import ClassVar

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))


def _load(name: str, relative: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    if spec is None or spec.loader is None:
        raise ImportError(relative)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


delete = _load("delete_inherited", "scripts/release/delete-inherited-upstream-tags.py")
guard = _load("check_push_tags", "scripts/git-hooks/check-push-tags.py")

A = "a" * 40
B = "b" * 40
C = "c" * 40
ZERO = "0" * 40


def listing(tags: dict[str, str]) -> str:
    return "".join(f"{sha}\trefs/tags/{name}\n" for name, sha in tags.items())


class DeleteSelection(unittest.TestCase):
    def test_matching_tag_is_selected(self) -> None:
        plan = delete.select({"v1.0.2": A, "v1.0.0-rc.1": B}, {"v1.0.2": A})
        self.assertEqual(plan.delete, ("v1.0.2",))
        self.assertEqual(plan.kept, ("v1.0.0-rc.1",))
        self.assertEqual(plan.refused, ())

    def test_same_name_other_sha_is_refused(self) -> None:
        plan = delete.select({"v1.0.2": B}, {"v1.0.2": A})
        self.assertEqual(plan.delete, ())
        self.assertEqual(plan.refused, (("v1.0.2", B, A),))

    def test_peeled_lines_are_ignored(self) -> None:
        tags = delete.parse_ls_remote(f"{A}\trefs/tags/x\n{B}\trefs/tags/x^{{}}\n")
        self.assertEqual(tags, {"x": A})

    def test_dry_run_deletes_nothing_and_apply_does(self) -> None:
        calls: list[str] = []
        fork = listing({"v1.0.2": A, "v1.0.0-rc.1": B})
        upstream = listing({"v1.0.2": A})
        self.assertEqual(delete.run(fork, upstream, False, calls.append), 0)
        self.assertEqual(calls, [])
        self.assertEqual(delete.run(fork, upstream, True, calls.append), 0)
        self.assertEqual(calls, ["v1.0.2"])

    def test_refusal_blocks_the_whole_apply(self) -> None:
        calls: list[str] = []
        fork = listing({"v1.0.2": A, "v2.0.0": C})
        upstream = listing({"v1.0.2": A, "v2.0.0": B})
        self.assertEqual(delete.run(fork, upstream, True, calls.append), 2)
        self.assertEqual(calls, [])


class PushGuard(unittest.TestCase):
    known: ClassVar[dict[str, set[str]]] = {"v1.0.2": {A}, "v3.0.0-rc": {B}}

    def line(self, name: str, sha: str) -> str:
        return f"refs/tags/{name} {sha} refs/tags/{name} {ZERO}"

    def run_guard(self, name: str, sha: str, peeled: str | None = None) -> list[str]:
        problems: list[str] = guard.check([self.line(name, sha)], self.known, lambda s: peeled or s)
        return problems

    def test_netflix_tag_refused(self) -> None:
        self.assertEqual(len(self.run_guard("v1.0.2", A)), 1)

    def test_netflix_tag_refused_by_peeled_commit(self) -> None:
        self.assertEqual(len(self.run_guard("v1.0.2", C, peeled=A)), 1)

    def test_fork_tag_with_netflix_name_on_other_object_allowed(self) -> None:
        self.assertEqual(self.run_guard("v1.0.2", C), [])

    def test_stray_names_refused(self) -> None:
        for name in ("v1.3.6rc", "v3.0.0-rc", "foo", "v1.0", "release-1"):
            self.assertEqual(len(self.run_guard(name, C)), 1, name)

    def test_fork_names_allowed(self) -> None:
        for name in (
            "v1.0.0-rc.3",
            "v1.0.0",
            "tester-20261004-4d3792b3",
            "tester-windows-20261004-2889f963",
            "archive/eupl-relicense-v1",
            "tiny-blobs-v1",
        ):
            self.assertEqual(self.run_guard(name, C), [], name)

    def test_delete_and_branch_pushes_allowed(self) -> None:
        deleting = f"(delete) {ZERO} refs/tags/v1.0.2 {A}"
        branch = f"refs/heads/x {A} refs/heads/x {ZERO}"
        self.assertEqual(guard.check([deleting, branch], self.known), [])

    def test_every_recorded_tag_is_refused_and_inventory_is_consistent(self) -> None:
        known = guard.load_inventory()
        data = json.loads(guard.INVENTORY.read_text(encoding="utf-8"))
        self.assertEqual(len(data["removed"]), 26)
        for row in data["removed"]:
            self.assertEqual(row["object"], row["netflix_object"])
            problems = guard.check(
                [f"x {row['object']} refs/tags/{row['name']} {ZERO}"], known, lambda s: s
            )
            self.assertEqual(len(problems), 1, row["name"])


if __name__ == "__main__":
    unittest.main()
