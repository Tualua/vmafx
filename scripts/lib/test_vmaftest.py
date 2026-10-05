#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Positive, negative and boundary controls for scripts/lib/vmaftest.py."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.lib import vmaftest

ROOT = Path(__file__).resolve().parents[2]


def _executable(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    path.chmod(0o755)
    return path


class FindTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.root = self.tmp / "repo"
        self.root.mkdir()

    def build(self, build_dir: str) -> Path:
        return _executable(self.root / build_dir / "tools" / vmaftest.executable_name())

    def test_vmaf_bin_is_used_before_everything_else(self) -> None:
        explicit = _executable(self.tmp / "explicit" / "vmaf")
        for_tests = _executable(self.tmp / "for-tests" / "vmaf")
        self.build("build")
        environ = {"VMAF_BIN": str(explicit), "VMAF_BIN_FOR_TESTS": str(for_tests)}
        self.assertEqual(vmaftest.find(environ, self.root), explicit)

    def test_vmaf_bin_for_tests_is_used_when_vmaf_bin_is_unset_or_empty(self) -> None:
        for_tests = _executable(self.tmp / "for-tests" / "vmaf")
        self.build("build")
        self.assertEqual(
            vmaftest.find({"VMAF_BIN_FOR_TESTS": str(for_tests)}, self.root), for_tests
        )
        environ = {"VMAF_BIN": "", "VMAF_BIN_FOR_TESTS": str(for_tests)}
        self.assertEqual(vmaftest.find(environ, self.root), for_tests)

    def test_a_set_variable_naming_a_missing_path_is_an_error_not_a_fallthrough(self) -> None:
        self.build("build")
        for name in vmaftest.ENV_VARS:
            with self.subTest(variable=name):
                environ = {name: str(self.tmp / "missing" / "vmaf")}
                with self.assertRaisesRegex(vmaftest.InvalidVmafBinary, name):
                    vmaftest.find(environ, self.root)

    def test_a_directory_is_not_a_vmaf_binary(self) -> None:
        with self.assertRaises(vmaftest.InvalidVmafBinary):
            vmaftest.find({"VMAF_BIN": str(self.tmp)}, self.root)

    @unittest.skipIf(os.name == "nt", "Windows files have no executable bit")
    def test_a_file_without_the_executable_bit_is_not_a_vmaf_binary(self) -> None:
        plain = self.tmp / "vmaf"
        plain.write_text("not a program\n", encoding="utf-8")
        plain.chmod(0o644)
        with self.assertRaises(vmaftest.InvalidVmafBinary):
            vmaftest.find({"VMAF_BIN": str(plain)}, self.root)

    def test_a_relative_value_is_read_from_the_start_directory_not_the_current_one(self) -> None:
        start = self.tmp / "start"
        binary = _executable(start / "build" / "tools" / "vmaf")
        elsewhere = self.tmp / "elsewhere"
        elsewhere.mkdir()
        previous = Path.cwd()
        self.addCleanup(os.chdir, previous)
        os.chdir(elsewhere)
        with mock.patch.object(vmaftest, "_START_DIR", start):
            found = vmaftest.find({"VMAF_BIN": "build/tools/vmaf"}, self.root)
        self.assertEqual(found, binary)

    def test_build_directories_are_searched_in_their_documented_order(self) -> None:
        self.assertEqual(vmaftest.BUILD_DIRS, ("build", "core/build", "core/build-cpu"))
        built = []
        for build_dir in reversed(vmaftest.BUILD_DIRS):
            built.append(self.build(build_dir))
            with self.subTest(newest=build_dir):
                self.assertEqual(vmaftest.find({}, self.root), built[-1])

    def test_nothing_set_and_no_build_is_none(self) -> None:
        self.assertIsNone(vmaftest.find({}, self.root))

    def test_path_is_never_searched(self) -> None:
        on_path = _executable(self.tmp / "bin" / vmaftest.executable_name())
        which = mock.Mock(return_value=str(on_path))
        with (
            mock.patch.dict(os.environ, {"PATH": str(on_path.parent)}),
            mock.patch.object(shutil, "which", which),
        ):
            self.assertIsNone(vmaftest.find({"PATH": str(on_path.parent)}, self.root))
        which.assert_not_called()


class MissingMessageTests(unittest.TestCase):
    def test_message_names_both_variables_and_the_build_command(self) -> None:
        for needle in ("VMAF_BIN ", "VMAF_BIN_FOR_TESTS", vmaftest.BUILD_COMMAND):
            with self.subTest(needle=needle):
                self.assertIn(needle, vmaftest.MISSING_MESSAGE)

    def test_message_fails_every_suite_that_declares_a_missing_binary_fatal(self) -> None:
        manifest = json.loads((ROOT / ".github" / "test-suites.json").read_text(encoding="utf-8"))
        declared = [
            suite for suite in manifest["suites"] if "vmaf binary" in suite.get("fail_on_skip", "")
        ]
        self.assertEqual(sorted(suite["name"] for suite in declared), ["ai", "mcp", "vmaf-tune"])
        for suite in declared:
            with self.subTest(suite=suite["name"]):
                pattern: str = suite["fail_on_skip"]
                self.assertRegex(vmaftest.MISSING_MESSAGE, pattern)


if __name__ == "__main__":
    unittest.main()
