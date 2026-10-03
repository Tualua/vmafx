# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The unit executables and the Netflix golden gate, run inside the tester image."""

from __future__ import annotations

import json
import os
import re
import site
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections.abc import Callable, Mapping
from pathlib import Path
from typing import Any

from .hw_equiv import Runner
from .safe_process import run_bounded

MESON_SKIP = 77
MAX_FAILURE_NAMES = 50
MAX_CASE_MESSAGE = 300
# One line per case of the Metal parity tests (core/test/metal_twin.h, ADR-1496).
CASE_LINE = re.compile(r"^@case (test_\w+) (pass|fail|skip)$")
MESSAGE_LINE = re.compile(r"^@message (test_\w+) (.*)$")
GOLDEN_FILES = (
    "python/test/quality_runner_test.py",
    "python/test/feature_extractor_test.py",
    "python/test/vmafexec_test.py",
    "python/test/vmafexec_feature_extractor_test.py",
    "python/test/result_test.py",
)  # same list as GOLDEN_PYTEST_ARGS in the Makefile


def _empty(reason: str | None = None) -> dict[str, Any]:
    return {"status": "not_run", "reason": reason, "total": 0, "passed": 0, "failed": 0,
            "skipped": 0, "failures": []}  # fmt: skip


def _finish(counts: dict[str, Any]) -> dict[str, Any]:
    counts["failures"] = counts["failures"][:MAX_FAILURE_NAMES]
    ran = counts["passed"] + counts["failed"]
    if counts["failed"] or ran == 0:
        counts["status"] = "fail"
    else:
        counts["status"] = "pass"
    return counts


def parse_case_lines(text: str) -> tuple[dict[str, str], dict[str, str]]:
    """Verdict of every case an executable printed, and the failures' messages."""
    verdicts: dict[str, str] = {}
    messages: dict[str, str] = {}
    for line in text.splitlines():
        match = CASE_LINE.match(line.strip())
        if match:
            verdicts[match.group(1)] = match.group(2)
            continue
        match = MESSAGE_LINE.match(line.strip())
        if match:
            messages[match.group(1)] = match.group(2)[:MAX_CASE_MESSAGE]
    return verdicts, messages


def _record_cases(counts: dict[str, Any], name: str, output: str) -> None:
    verdicts, messages = parse_case_lines(output)
    if verdicts:
        counts.setdefault("cases", {})[name] = verdicts
    if messages:
        counts.setdefault("case_messages", {})[name] = messages


def test_environment(
    test: Mapping[str, Any], root: Path, work: Path, extra: Mapping[str, str] | None
) -> dict[str, str] | None:
    """The process environment of one test: the report's own, the device selection
    (`extra`) and the test's Meson environment with the manifest's placeholders
    ({root} the image root, {work} the test's scratch directory) resolved.
    None keeps the report's environment unchanged (the CPU unit tests)."""
    own = test.get("env") or {}
    if not own and not extra:
        return None
    env = dict(os.environ)
    env.update(extra or {})
    for key, value in own.items():
        value = str(value).replace("{root}", str(root)).replace("{work}", str(work))
        if key.endswith("PATH") and env.get(key):  # Meson prepends search paths
            value = value + os.pathsep + env[key]
        env[str(key)] = value
    return env


def run_one_test(
    test: Mapping[str, Any],
    root: Path,
    *,
    timeout_seconds: float,
    runner: Runner,
    environment: Mapping[str, str] | None,
) -> tuple[int, str]:
    """(exit code, stderr and stdout) of one manifest entry; -1 when it could not run.

    Every test runs in a fresh directory (the image is read-only and some tests
    write next to themselves). With `scratch` it also holds `tools` -> the image's
    build/tools, which is how the Meson shell tests find `./tools/vmaf`."""
    limit = timeout_seconds
    if "timeout" in test:  # Meson's per-test limit with its usual headroom
        limit = min(timeout_seconds, 4.0 * float(test["timeout"]))
    with tempfile.TemporaryDirectory(prefix="vmaf-test-") as work_dir:
        work = Path(work_dir)
        if test.get("scratch"):
            (work / "tools").symlink_to(root / "build" / "tools")
        kwargs: dict[str, Any] = {"timeout_seconds": limit, "max_output_bytes": 1_048_576}
        env = test_environment(test, root, work, environment)
        if env is not None:
            kwargs["environment"] = env
        kwargs["cwd"] = str(work)
        argv = [str(test["cmd"]), *[str(arg) for arg in test.get("args", [])]]
        try:
            result = runner(argv, **kwargs)
        except (TimeoutError, RuntimeError, ValueError, OSError):
            return -1, ""
    return result.returncode, (result.stderr or "") + "\n" + (result.stdout or "")


def _count(counts: dict[str, Any], name: str, code: int) -> None:
    results = counts.setdefault("results", {})
    if code == 0:
        counts["passed"] += 1
        results[name] = "pass"
    elif code == MESON_SKIP:
        counts["skipped"] += 1
        counts.setdefault("skipped_tests", []).append(name)
        results[name] = "skip"
    else:
        counts["failed"] += 1
        counts["failures"].append(name)
        results[name] = "fail"


def run_unit_tests(
    manifest: Path,
    *,
    timeout_seconds: float,
    runner: Runner = run_bounded,
    environment: Mapping[str, str] | None = None,
    observe: Callable[[str, str], None] | None = None,
) -> dict[str, Any]:
    """Run each executable of the baked manifest; exit 77 counts as skipped.

    The verdict lines of tests that print one per case (`@case`) are kept under
    `cases`, so the report can say which state rows a run measured (ADR-1496).
    `environment` adds variables to every test (the GPU section's device
    selection); `observe(name, output)` sees each test's output (the audits).
    Tests the manifest lists as left out are carried as `left_out`."""
    try:
        document = json.loads(manifest.read_text(encoding="utf-8"))
        tests = document["tests"]
    except (OSError, ValueError, KeyError) as error:
        return _empty(f"unit test manifest unreadable: {error}")
    root = manifest.resolve().parents[1]
    counts = _empty()
    counts["total"] = len(tests)
    for test in tests:
        name = str(test["name"])
        code, output = run_one_test(
            test, root, timeout_seconds=timeout_seconds, runner=runner, environment=environment
        )
        _record_cases(counts, name, output)
        if observe is not None:
            observe(name, output)
        _count(counts, name, code)
    if document.get("left_out"):
        counts["left_out"] = [dict(item) for item in document["left_out"]]
    return _finish(counts)


def parse_junit(path: Path) -> dict[str, Any]:
    """Counts and failing test names from a pytest JUnit file."""
    counts = _empty()
    root = ET.parse(path).getroot()  # the file is written by our own pytest run
    for case in root.iter("testcase"):
        name = f"{case.get('classname', '')}::{case.get('name', '')}"
        counts["total"] += 1
        if case.find("skipped") is not None:
            counts["skipped"] += 1
        elif case.find("failure") is not None or case.find("error") is not None:
            counts["failed"] += 1
            counts["failures"].append(name)
        else:
            counts["passed"] += 1
    return _finish(counts)


def golden_environment(image_root: Path, build_dir: Path, workspace: Path) -> dict[str, str]:
    """Environment of `make test-netflix-golden`, pointed at the image layout."""
    env = {key: os.environ[key] for key in ("PATH", "LANG", "LC_ALL") if key in os.environ}
    env.update(
        {
            "CUDA_VISIBLE_DEVICES": "",
            "VMAF_FORCE_BACKEND": "cpu",
            "VMAF_BUILD_DIR": str(build_dir),
            "VMAF_WORKSPACE": str(workspace),
            # run_bounded resolves symlinks, so a venv's `python` starts as the base
            # interpreter: hand it the venv's packages explicitly.
            "PYTHONPATH": os.pathsep.join([str(image_root / "python"), *site.getsitepackages()]),
            "PYTHONDONTWRITEBYTECODE": "1",
            "HOME": str(workspace),
            "MPLCONFIGDIR": str(workspace),
            "LD_LIBRARY_PATH": f"{build_dir / 'src'}:{build_dir.parent / 'lib'}",
        }
    )
    return env


def run_golden_gate(
    image_root: Path, *, timeout_seconds: float, runner: Runner = run_bounded
) -> dict[str, Any]:
    """The five Netflix golden test files, `-m "not slow"`, as the Makefile runs them."""
    if not (image_root / "python" / "test").is_dir():
        return _empty("python golden tests are not part of this image")
    with tempfile.TemporaryDirectory(prefix="vmaf-golden-") as work:
        junit = Path(work) / "junit.xml"
        argv = [sys.executable, "-m", "pytest", *GOLDEN_FILES, "-m", "not slow", "-q", "--tb=no",
                "-o", f"cache_dir={work}/pytest-cache", f"--junitxml={junit}"]  # fmt: skip
        env = golden_environment(image_root, image_root / "build", Path(work))
        try:
            runner(argv, environment=env, timeout_seconds=timeout_seconds,
                   max_output_bytes=1_048_576, cwd=str(image_root))  # fmt: skip
            return parse_junit(junit)
        except (TimeoutError, RuntimeError, ValueError, OSError, ET.ParseError) as error:
            result = _empty(f"golden gate did not finish: {error}")
            result["status"] = "fail"
            return result


def summary_line(name: str, section: Mapping[str, Any]) -> str:
    """One human line for stderr."""
    return (
        f"{name}: {section['status']} "
        f"(passed {section['passed']}, failed {section['failed']}, skipped {section['skipped']})"
    )
