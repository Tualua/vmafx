#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The test-suite registry: every tracked test file runs in a required check.

``.github/test-suites.json`` maps each test file of the repository to one
suite and each suite to the required checks that run it (ADR-1528). Two
commands use it:

``check``
    Fails when a tracked test file belongs to no suite or to two, when a
    suite or ``not_tests`` entry matches no tracked file, or when a suite names
    a check the Required Checks Aggregator does not require. A test directory
    nobody wired into CI is how ``tools/vmaf-tune/tests`` (2051 tests) and 60
    script tests went unrun; this check makes that a red pull request.

``run SUITE``
    Runs one suite: one pytest run over its Python test files, then every
    shell test file with ``bash``. CI runs ``run tooling``.

``list SUITE``
    Prints the test files of one suite, one per line.

Exit status: 0 success, 1 a finding or a failed test, 2 a usage or I/O error.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import sys
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.lib.safe_subprocess import CommandTimedOut
from scripts.lib.safe_subprocess import run as run_command

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(".github/test-suites.json")
AGGREGATOR = Path(".github/workflows/required-aggregator.yml")
GIT_TIMEOUT_SECONDS = 60.0
SHELL_TEST_TIMEOUT_SECONDS = 600.0
PER_TEST_TIMEOUT_SECONDS = 300


class RegistryError(Exception):
    """The registry or the repository could not be read."""


@dataclass(frozen=True)
class Suite:
    name: str
    paths: tuple[str, ...]
    checks: tuple[str, ...]


@dataclass(frozen=True)
class Registry:
    name_patterns: tuple[str, ...]
    path_patterns: tuple[str, ...]
    suites: tuple[Suite, ...]
    not_tests: tuple[str, ...]


def _no_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    keys = [key for key, _ in pairs]
    duplicates = sorted({key for key in keys if keys.count(key) > 1})
    if duplicates:
        raise RegistryError(f"duplicate keys in {MANIFEST}: {duplicates}")
    return dict(pairs)


def _strings(value: Any, label: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not value or not all(isinstance(v, str) and v for v in value):
        raise RegistryError(f"{label} must be a non-empty list of non-empty strings")
    return tuple(value)


def _suite(entry: Any, index: int) -> Suite:
    if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
        raise RegistryError(f"suites[{index}] must be an object with a string name")
    label = f"suites[{index}] ({entry['name']})"
    return Suite(
        name=entry["name"],
        paths=_strings(entry.get("paths"), f"{label}.paths"),
        checks=_strings(entry.get("checks"), f"{label}.checks"),
    )


def load_registry(root: Path) -> Registry:
    """Parse the manifest, refusing duplicate keys and malformed entries."""
    try:
        text = (root / MANIFEST).read_text(encoding="utf-8")
        data = json.loads(text, object_pairs_hook=_no_duplicate_keys)
    except (OSError, ValueError) as exc:
        raise RegistryError(f"cannot read {MANIFEST}: {exc}") from exc
    if data.get("schema_version") != 1:
        raise RegistryError(f"{MANIFEST}: schema_version must be 1")
    suites = tuple(_suite(entry, i) for i, entry in enumerate(data.get("suites") or []))
    names = [suite.name for suite in suites]
    if not suites or len(set(names)) != len(names):
        raise RegistryError(f"{MANIFEST}: suites must be non-empty with unique names")
    not_tests = data.get("not_tests", [])
    if not all(isinstance(e, dict) and e.get("path") and e.get("reason") for e in not_tests):
        raise RegistryError(f"{MANIFEST}: every not_tests entry needs a path and a reason")
    return Registry(
        name_patterns=_strings(data.get("name_patterns"), "name_patterns"),
        path_patterns=tuple(data.get("path_patterns", [])),
        suites=suites,
        not_tests=tuple(e["path"] for e in not_tests),
    )


def tracked_files(root: Path) -> list[str]:
    """Every file git tracks under root, as repository-relative POSIX paths."""
    git = shutil.which("git")
    if git is None:
        raise RegistryError("git is not on PATH")
    # A hook parent's GIT_DIR / GIT_INDEX_FILE would point git at another repo.
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    result = run_command(
        [git, "-C", str(root), "ls-files", "-z"],
        allowed_executables=(git,),
        env=env,
        capture_output=True,
        text=True,
        check=False,
        timeout_seconds=GIT_TIMEOUT_SECONDS,
    )
    if result.returncode != 0:
        raise RegistryError(f"git ls-files failed in {root}: {result.stderr.strip()}")
    return [path for path in result.stdout.split("\0") if path]


def _under(path: str, prefix: str) -> bool:
    return path == prefix or (prefix.endswith("/") and path.startswith(prefix))


def is_test_file(registry: Registry, path: str) -> bool:
    name = path.rsplit("/", 1)[-1]
    return any(fnmatch.fnmatchcase(name, p) for p in registry.name_patterns) or any(
        fnmatch.fnmatchcase(path, p) for p in registry.path_patterns
    )


def required_checks(root: Path) -> set[str]:
    """The names in the aggregator's `required` list (as check-aggregator-names.sh reads them)."""
    try:
        text = (root / AGGREGATOR).read_text(encoding="utf-8")
    except OSError as exc:
        raise RegistryError(f"cannot read {AGGREGATOR}: {exc}") from exc
    match = re.search(r"const required = \[(.*?)\];", text, re.DOTALL)
    if match is None:
        raise RegistryError(f"{AGGREGATOR} has no `const required = [...]` list")
    body = re.sub(r"(?m)^\s*//.*$", "", match.group(1))
    return set(re.findall(r"'([^']+)'", body))


def _owners(registry: Registry, path: str) -> list[str]:
    return [s.name for s in registry.suites if any(_under(path, p) for p in s.paths)]


def file_findings(registry: Registry, files: Iterable[str]) -> list[str]:
    """Test files outside every suite or inside two; not_tests entries that match nothing."""
    findings = []
    used_exclusions = set()
    for path in files:
        excluded = [e for e in registry.not_tests if _under(path, e)]
        used_exclusions.update(excluded)
        if excluded or not is_test_file(registry, path):
            continue
        owners = _owners(registry, path)
        if len(owners) != 1:
            where = "no suite" if not owners else f"suites {owners}"
            findings.append(f"{path}: test file in {where}; add it to one suite of {MANIFEST}")
    for entry in sorted(set(registry.not_tests) - used_exclusions):
        findings.append(f"not_tests entry {entry!r} matches no tracked file; remove it")
    return findings


def suite_findings(registry: Registry, files: Sequence[str], required: set[str]) -> list[str]:
    """Suite paths that hold no test file; checks the aggregator does not require."""
    findings = []
    tests = [f for f in files if is_test_file(registry, f)]
    for suite in registry.suites:
        for prefix in suite.paths:
            if not any(_under(f, prefix) for f in tests):
                findings.append(f"suite {suite.name}: path {prefix!r} holds no test file")
        for check in suite.checks:
            if check not in required:
                findings.append(
                    f"suite {suite.name}: check {check!r} is not in the required list of "
                    f"{AGGREGATOR}, so a red run would not block a merge"
                )
    return findings


def suite_files(registry: Registry, files: Iterable[str], name: str) -> list[str]:
    """The test files of one suite, sorted, without not_tests entries."""
    suite = next((s for s in registry.suites if s.name == name), None)
    if suite is None:
        raise RegistryError(f"no suite named {name!r} in {MANIFEST}")
    return sorted(
        path
        for path in files
        if is_test_file(registry, path)
        and any(_under(path, p) for p in suite.paths)
        and not any(_under(path, e) for e in registry.not_tests)
    )


def command_check(root: Path) -> int:
    registry = load_registry(root)
    files = tracked_files(root)
    findings = file_findings(registry, files)
    findings += suite_findings(registry, files, required_checks(root))
    for finding in findings:
        print(f"suite-registry: {finding}", file=sys.stderr)
    if findings:
        print(f"suite-registry: {len(findings)} finding(s); see ADR-1528", file=sys.stderr)
        return 1
    count = sum(len(suite_files(registry, files, suite.name)) for suite in registry.suites)
    print(f"suite-registry: OK ({count} test files in {len(registry.suites)} suites)")
    return 0


def _run_pytest(root: Path, files: Sequence[str]) -> bool:
    """One in-process pytest run over files, from the repository root.

    In-process because safe_subprocess runs the resolved interpreter, which
    would leave the virtual environment that holds the tooling lock. Every
    test has a pytest-timeout deadline, so a hung test fails instead of
    holding the job.
    """
    if not files:
        return True
    import pytest  # noqa: PLC0415 - the check command must not need pytest

    print(f"suite-registry: pytest over {len(files)} Python test files", flush=True)
    os.chdir(root)
    status = int(pytest.main(["-rs", f"--timeout={PER_TEST_TIMEOUT_SECONDS}", *files]))
    return status == 0


def _run_shell_test(root: Path, bash: str, path: str) -> bool:
    try:
        result = run_command(
            [bash, path],
            allowed_executables=(bash,),
            cwd=root,
            input_data="",
            capture_output=True,
            stderr_to_stdout=True,
            text=True,
            check=False,
            timeout_seconds=SHELL_TEST_TIMEOUT_SECONDS,
        )
    except CommandTimedOut:
        print(f"FAIL {path}: exceeded {SHELL_TEST_TIMEOUT_SECONDS:g}s", flush=True)
        return False
    if result.returncode == 0:
        print(f"ok   {path}", flush=True)
        return True
    print(f"FAIL {path}: exit {result.returncode}\n{result.stdout}", flush=True)
    return False


def command_run(root: Path, name: str) -> int:
    registry = load_registry(root)
    files = suite_files(registry, tracked_files(root), name)
    python_files = [f for f in files if f.endswith(".py")]
    shell_files = [f for f in files if f.endswith(".sh")]
    others = sorted(set(files) - set(python_files) - set(shell_files))
    if others:
        raise RegistryError(f"suite {name} holds files this runner cannot run: {others}")
    bash = shutil.which("bash")
    if bash is None:
        raise RegistryError("bash is not on PATH")
    failed = [] if _run_pytest(root, python_files) else ["pytest"]
    failed += [path for path in shell_files if not _run_shell_test(root, bash, path)]
    print(f"suite-registry: {name}: {len(python_files)} Python and {len(shell_files)} shell files")
    if failed:
        print(f"suite-registry: {name}: failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="repository root")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser(
        "check", help="every test file belongs to one suite run by a required check"
    )
    run = commands.add_parser(
        "run", help="run one suite (Python files with pytest, shell files with bash)"
    )
    run.add_argument("suite")
    listing = commands.add_parser("list", help="print the test files of one suite")
    listing.add_argument("suite")
    return parser.parse_args(argv)


def command_list(root: Path, name: str) -> int:
    for path in suite_files(load_registry(root), tracked_files(root), name):
        print(path)
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "check":
            return command_check(args.root)
        if args.command == "list":
            return command_list(args.root, args.suite)
        return command_run(args.root, args.suite)
    except RegistryError as exc:
        print(f"suite-registry: error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
