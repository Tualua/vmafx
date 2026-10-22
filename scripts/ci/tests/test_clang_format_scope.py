# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The clang-format hook, `make format` and the native hook read `.hip` and `.metal` (HISS-15).

pre-commit's `types_or` cannot select either extension (identify has no tag for them), so a
second hook entry reads them by extension. These cases pin that entry, the Makefile list that
`format` and `format-check` share, and the native hook's regex, and refuse each way the files
fall out of scope again.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import unittest
from pathlib import Path
from typing import Any

import yaml  # type: ignore[import-untyped]  # PyYAML ships no stubs in the hook env, as in scripts/ci/check-helm-selector-isolation.py

ROOT = Path(__file__).resolve().parents[3]
HOOK_ALIAS = "clang-format-hip-metal"


def clang_format_hooks() -> list[dict[str, Any]]:
    config = yaml.safe_load((ROOT / ".pre-commit-config.yaml").read_text(encoding="utf-8"))
    hooks: list[dict[str, Any]] = []
    for repo in config["repos"]:
        if repo["repo"].endswith("mirrors-clang-format"):
            hooks.extend(repo["hooks"])
    return hooks


def hip_metal_hook() -> dict[str, Any]:
    found = [h for h in clang_format_hooks() if h.get("alias") == HOOK_ALIAS]
    if len(found) != 1:
        raise AssertionError(f"expected one {HOOK_ALIAS} hook, found {len(found)}")
    return found[0]


def selected(hook: dict[str, Any], path: str) -> bool:
    """pre-commit's rule: `files` matches and `exclude` does not (types: [file] matches all)."""
    if not re.search(hook.get("files", ""), path):
        return False
    return not (hook.get("exclude") and re.search(hook["exclude"], path))


class ClangFormatScopeTests(unittest.TestCase):
    def test_hook_selects_hip_and_metal(self) -> None:
        hook = hip_metal_hook()
        for path in ("core/src/feature/hip/float_adm/a.hip", "core/src/feature/metal/a.metal"):
            self.assertTrue(selected(hook, path), path)

    def test_hook_refuses_other_files_and_the_data_fragments(self) -> None:
        hook = hip_metal_hook()
        for path in ("a.c", "a.hipx", "a.metalx", "a.hip.txt", "scripts/ci/exact_twins.d/adm.hip"):
            self.assertFalse(selected(hook, path), path)

    def test_hook_uses_the_same_wrapper_and_pin_as_the_c_family_entry(self) -> None:
        hooks = clang_format_hooks()
        self.assertEqual(
            {h["entry"] for h in hooks}, {"python3 scripts/ci/pelorus_mirror.py clang-format"}
        )
        self.assertTrue(any(h.get("types_or") for h in hooks), "the C-family entry is gone")

    def test_every_tracked_hip_and_metal_source_is_selected(self) -> None:
        git = shutil.which("git")
        self.assertIsNotNone(git, "git is not on PATH")
        hook = hip_metal_hook()
        out = subprocess.run(  # noqa: S603 -- resolved git, fixed argv
            [str(git), "-C", str(ROOT), "ls-files", "*.hip", "*.metal"],
            capture_output=True,
            text=True,
            check=True,
            timeout=60,
        ).stdout.split()
        sources = [p for p in out if not p.startswith("scripts/ci/exact_twins.d/")]
        self.assertGreater(len(sources), 30)
        self.assertEqual([p for p in sources if not selected(hook, p)], [])

    def test_makefile_format_targets_share_one_list_with_hip_and_metal(self) -> None:
        text = (ROOT / "Makefile").read_text(encoding="utf-8")
        match = re.search(r"^CLANG_FORMAT_FILES = (.*?)\n\n", text, re.M | re.S)
        self.assertIsNotNone(match, "CLANG_FORMAT_FILES is not defined")
        assert match is not None
        listing = match.group(1)
        for needle in ("'*.hip'", "'*.metal'", "exact_twins", "pelorus_mirror.py filter"):
            self.assertIn(needle, listing)
        self.assertEqual(text.count("$(CLANG_FORMAT_FILES)"), 2)
        self.assertNotIn(
            "git ls-files '*.c' '*.h' '*.cpp' '*.hpp' '*.cu' '*.cuh' \\\n\t                   |",
            text,
        )

    def test_native_hook_reads_hip_and_metal(self) -> None:
        text = (ROOT / "scripts/githooks/pre-commit.sh").read_text(encoding="utf-8")
        self.assertIn(r"\.(c|h|cpp|hpp|cc|cu|cuh|hip|metal)$", text)
        self.assertIn(r"scripts/ci/exact_twins\.d/", text)


if __name__ == "__main__":
    unittest.main()
