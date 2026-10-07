# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

"""Warnings are errors per leg (ADR-2170): the helper script and the legs that call it."""

from __future__ import annotations

import os
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts/ci/werror-args.sh"
MATRIX = ROOT / ".github/workflows/libvmaf-build-matrix.yml"
CI_DOC = ROOT / "docs/development/ci.md"
WORKFLOWS = ROOT / ".github/workflows"

# The legs that stay ungated, with the reason ci.md gives. A row not named here must carry
# `werror: true`, so a new leg cannot be added without deciding.
UNGATED_ROWS = {"macOS Metal"}


def run_script(*args: str, os_name: str | None = None) -> subprocess.CompletedProcess[str]:
    env = {key: value for key, value in os.environ.items() if key != "WERROR_ARGS_OS"}
    if os_name is not None:
        env["WERROR_ARGS_OS"] = os_name
    return subprocess.run(  # noqa: S603 -- the script path is this repository's own.
        [str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )


def matrix_rows(text: str) -> dict[str, bool]:
    """Name -> carries `werror: true`, for every row of the build matrix."""
    rows: dict[str, bool] = {}
    for block in re.split(r"\n\s+- os: ", text)[1:]:
        name = re.search(r"^\s+name: (.+)$", block, re.M)
        if name and "CC:" in block:
            rows[name.group(1).strip()] = bool(re.search(r"^\s+werror: true$", block, re.M))
    return rows


def contract_failures(matrix: str, doc: str) -> list[str]:
    failures: list[str] = []
    rows = matrix_rows(matrix)
    if not rows:
        return ["no build-matrix rows found"]
    for name, gated in sorted(rows.items()):
        if gated and name in UNGATED_ROWS:
            failures.append(f"{name} is ungated in this test but carries werror: true")
        if not gated and name not in UNGATED_ROWS:
            failures.append(f"{name} has no werror: true and is not in UNGATED_ROWS")
        if name in UNGATED_ROWS and name not in doc:
            failures.append(f"{name} is ungated but docs/development/ci.md does not list it")
    if 'werror-args.sh "${{ matrix.werror }}"' not in matrix:
        failures.append("the matrix configure step does not call werror-args.sh")
    return failures


class WerrorArgsScriptTests(unittest.TestCase):
    def test_true_prints_werror_and_the_linker_switch_per_os(self) -> None:
        for os_name, fatal in (
            ("Linux", "-Wl,--fatal-warnings"),
            ("MINGW64_NT-10.0", "-Wl,--fatal-warnings"),
            ("Darwin", "-Wl,-fatal_warnings"),
        ):
            with self.subTest(os=os_name):
                result = run_script("true", os_name=os_name)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    result.stdout.split(),
                    ["-Dwerror=true", f"-Dc_link_args={fatal}", f"-Dcpp_link_args={fatal}"],
                )

    def test_unset_or_false_prints_nothing(self) -> None:
        for args in ((), ("",), ("false",)):
            with self.subTest(args=args):
                result = run_script(*args, os_name="Linux")
                self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_a_typo_or_an_unknown_os_is_refused(self) -> None:
        for args, os_name in ((("True",), "Linux"), (("yes",), "Linux"), (("true",), "Plan9")):
            with self.subTest(args=args, os=os_name):
                result = run_script(*args, os_name=os_name)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertEqual(result.stdout, "")


class WerrorLegContractTests(unittest.TestCase):
    def test_every_matrix_row_is_gated_or_named_ungated(self) -> None:
        failures = contract_failures(
            MATRIX.read_text(encoding="utf-8"), CI_DOC.read_text(encoding="utf-8")
        )
        self.assertEqual(failures, [])

    def test_a_row_that_loses_its_gate_is_detected(self) -> None:
        matrix = MATRIX.read_text(encoding="utf-8")
        planted = matrix.replace(
            "            werror: true\n            name: Ubuntu gcc\n",
            "            name: Ubuntu gcc\n",
            1,
        )
        self.assertNotEqual(planted, matrix)
        failures = contract_failures(planted, CI_DOC.read_text(encoding="utf-8"))
        self.assertTrue(any("Ubuntu gcc has no werror" in item for item in failures), failures)

    def test_a_new_row_without_a_decision_is_detected(self) -> None:
        matrix = MATRIX.read_text(encoding="utf-8")
        planted = matrix.replace(
            "          - os: ubuntu-latest\n            CC: icx\n",
            "          - os: ubuntu-latest\n            CC: gcc\n            name: Ubuntu planted\n"
            "          - os: ubuntu-latest\n            CC: icx\n",
            1,
        )
        self.assertNotEqual(planted, matrix)
        failures = contract_failures(planted, CI_DOC.read_text(encoding="utf-8"))
        self.assertTrue(any("Ubuntu planted" in item for item in failures), failures)

    def test_every_call_names_the_script_that_exists(self) -> None:
        self.assertTrue(os.access(SCRIPT, os.X_OK), "scripts/ci/werror-args.sh is not executable")
        calls = 0
        for workflow in sorted(WORKFLOWS.glob("*.yml")):
            for match in re.finditer(
                r"\$\((?:bash )?((?:\.\./)?scripts/ci/werror-args\.sh)\b",
                workflow.read_text(encoding="utf-8"),
            ):
                calls += 1
                base = ROOT / "core" if match.group(1).startswith("../") else ROOT
                self.assertTrue(
                    (base / match.group(1)).resolve().is_file(),
                    f"{workflow.name}: {match.group(1)}",
                )
        self.assertGreaterEqual(calls, 6)


if __name__ == "__main__":
    unittest.main()
