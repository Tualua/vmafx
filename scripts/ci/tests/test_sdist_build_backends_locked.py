#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every sdist-only locked pin has its build backend in package-build.in and its lock.

Positive: the committed locks pass.  Negative: the locks before the poetry-core fix
(reuse 6.2.0 without its backend) and a backend missing from the lock fail.
Boundary: name normalisation, a pin whose marker excludes Linux, a record entry that
no lock pins any more, and a wheel tag the CI interpreter does not accept.
"""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
MODULE = ROOT / "scripts/ci/sdist_only_pins.py"
HASH = "    --hash=sha256:" + "0" * 64


def load() -> Any:
    spec = importlib.util.spec_from_file_location("sdist_only_pins", MODULE)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def lock(*pins: str) -> str:
    return "".join(f"{pin} \\\n{HASH}\n" for pin in pins)


class Tree:
    """A throwaway repository root with one lock per call."""

    def __init__(self, root: Path, sdist_only: dict[str, list[str]]) -> None:
        self.root = root
        (root / "requirements/locks").mkdir(parents=True)
        (root / "scripts/ci").mkdir(parents=True)
        doc = {"sdist_only": sdist_only}
        (root / "scripts/ci/sdist_only_pins.json").write_text(json.dumps(doc), encoding="utf-8")

    def put(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


class SdistBackendTests(unittest.TestCase):
    mod: Any

    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load()

    def tree(self, sdist_only: dict[str, list[str]], build_in: str, build_lock: list[str]) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        tree = Tree(Path(tmp.name), sdist_only)
        tree.put("requirements/locks/package-build.in", build_in)
        tree.put("requirements/locks/package-build.txt", lock(*build_lock))
        tree.put("tools/x/requirements-dev-lock.txt", lock("reuse==6.2.0"))
        return tree.root

    def test_committed_locks_have_every_sdist_backend(self) -> None:
        self.assertEqual(self.mod.check(ROOT), [])

    def test_committed_record_lists_reuse_with_poetry_core(self) -> None:
        record = json.loads((ROOT / self.mod.RECORD).read_text(encoding="utf-8"))["sdist_only"]
        self.assertEqual(record["reuse==6.2.0"], ["poetry-core"])

    def test_missing_backend_in_input_fails(self) -> None:
        root = self.tree({"reuse==6.2.0": ["poetry-core"]}, "hatchling==1\n", ["hatchling==1"])
        findings = self.mod.check(root)
        self.assertEqual(len(findings), 1)
        self.assertIn("reuse==6.2.0", findings[0])
        self.assertIn("poetry-core is not in requirements/locks/package-build.in", findings[0])

    def test_backend_in_input_but_not_in_lock_fails(self) -> None:
        root = self.tree({"reuse==6.2.0": ["poetry-core"]}, "poetry-core==2\n", ["hatchling==1"])
        findings = self.mod.check(root)
        self.assertEqual(len(findings), 1)
        self.assertIn("is not in requirements/locks/package-build.txt", findings[0])

    def test_present_backend_passes(self) -> None:
        root = self.tree({"reuse==6.2.0": ["poetry-core"]}, "poetry-core==2\n", ["poetry-core==2"])
        self.assertEqual(self.mod.check(root), [])

    def test_names_compare_normalised(self) -> None:
        root = self.tree({"reuse==6.2.0": ["Poetry_Core"]}, "poetry-core==2\n", ["poetry_core==2"])
        self.assertEqual(self.mod.check(root), [])

    def test_every_backend_of_a_pin_is_checked(self) -> None:
        record = {"reuse==6.2.0": ["poetry-core", "setuptools-scm"]}
        root = self.tree(record, "poetry-core==2\n", ["poetry-core==2"])
        self.assertEqual(len(self.mod.check(root)), 1)

    def test_record_entry_no_lock_pins_is_stale(self) -> None:
        root = self.tree({"reuse==6.1.0": ["poetry-core"]}, "poetry-core==2\n", ["poetry-core==2"])
        findings = self.mod.check(root)
        self.assertEqual(len(findings), 1)
        self.assertIn("no lock pins it", findings[0])

    def test_pin_excluded_by_marker_is_not_a_pin(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x-lock.txt"
            path.write_text(
                lock("colorama==0.4.6 ; os_name == 'nt'", "build==1.6.1"), encoding="utf-8"
            )
            self.assertEqual(self.mod.lock_pins(path), {("build", "1.6.1")})

    def test_wheel_tags_follow_the_ci_interpreter(self) -> None:
        accepted = self.mod.ci_tags()
        wheel = self.mod.utils.parse_wheel_filename
        self.assertFalse(
            accepted & set(wheel("reuse-6.2.0-cp310-cp310-manylinux_2_41_x86_64.whl")[3])
        )
        self.assertTrue(accepted & set(wheel("x-1-cp314-cp314-manylinux_2_28_x86_64.whl")[3]))
        self.assertTrue(accepted & set(wheel("x-1-cp312-abi3-manylinux2014_x86_64.whl")[3]))
        self.assertTrue(accepted & set(wheel("x-1-py3-none-any.whl")[3]))
        self.assertFalse(accepted & set(wheel("x-1-cp314-cp314-macosx_11_0_arm64.whl")[3]))


if __name__ == "__main__":
    unittest.main()
