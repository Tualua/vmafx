# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for the macOS bundle scripts, run on Linux with fake `otool`, `file` and `uname`."""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[3]
RUN_SH = _ROOT / "tools" / "rc1-tester" / "image" / "macos" / "run.sh"
LINKS = _ROOT / "scripts" / "ci" / "check-macos-bundle-links.sh"

pytestmark = pytest.mark.skipif(shutil.which("bash") is None, reason="needs bash")


def script(path: Path, body: str) -> None:
    path.write_text("#!/bin/sh\n" + body)
    path.chmod(path.stat().st_mode | stat.S_IEXEC)


def fake_tools(directory: Path, deps: dict[str, list[str]]) -> dict[str, str]:
    """`file` calls every *.macho a Mach-O; `otool -L` prints the canned dependencies."""
    directory.mkdir(parents=True)
    script(
        directory / "file",
        'case "$2" in *.macho) echo "Mach-O 64-bit arm64";; *) echo text;; esac\n',
    )
    for name, dependencies in deps.items():
        lines = [f"{name}:"] + [f"\t{dep} (compatibility version 1.0.0)" for dep in dependencies]
        (directory / f"{name}.otool").write_text("\n".join(lines) + "\n")
    script(directory / "otool", f'cat "{directory}/$(basename "$2").otool"\n')
    return {**os.environ, "PATH": f"{directory}:{os.environ['PATH']}"}


def make_bundle(root: Path) -> Path:
    for rel in ("build/tools/vmaf.macho", "tests/test_a.macho", "runtime/bin/python3.macho",
                "runtime/lib/libpython.macho"):  # fmt: skip
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text("x")
    return root


def check(tmp_path: Path, deps: dict[str, list[str]]) -> subprocess.CompletedProcess:
    tmp_path.mkdir(exist_ok=True)
    bundle = make_bundle(tmp_path / "bundle")
    env = fake_tools(tmp_path / "fake", deps)
    return subprocess.run(
        ["bash", str(LINKS), str(bundle)], env=env, capture_output=True, text=True, check=False
    )


SYSTEM = ["/usr/lib/libSystem.B.dylib", "/System/Library/Frameworks/Metal.framework/Metal"]


def test_system_only_links_pass(tmp_path: Path) -> None:
    deps = {n: SYSTEM for n in ("vmaf.macho", "test_a.macho", "python3.macho", "libpython.macho")}
    assert check(tmp_path, deps).returncode == 0


def test_homebrew_dependency_in_vmaf_fails(tmp_path: Path) -> None:
    deps = {n: SYSTEM for n in ("test_a.macho", "python3.macho", "libpython.macho")}
    deps["vmaf.macho"] = [*SYSTEM, "/opt/homebrew/opt/libomp/lib/libomp.dylib"]
    result = check(tmp_path, deps)
    assert result.returncode == 1 and "build/tools/vmaf.macho links /opt/homebrew" in result.stderr


def test_bundle_relative_reference_only_for_the_runtime(tmp_path: Path) -> None:
    base = {n: SYSTEM for n in ("vmaf.macho", "test_a.macho", "libpython.macho")}
    inside = {**base, "python3.macho": [*SYSTEM, "@executable_path/../lib/libpython.macho"]}
    assert check(tmp_path, inside).returncode == 0
    missing = {**base, "python3.macho": [*SYSTEM, "@executable_path/../lib/absent.dylib"]}
    assert check(tmp_path / "m", missing).returncode == 1
    strict = {**base, "python3.macho": SYSTEM, "vmaf.macho": [*SYSTEM, "@loader_path/x.dylib"]}
    assert check(tmp_path / "s", strict).returncode == 1


def test_run_sh_refuses_other_platforms() -> None:
    result = subprocess.run(["sh", str(RUN_SH)], capture_output=True, text=True, check=False)
    assert result.returncode == 64 and "Apple silicon" in result.stderr


def test_run_sh_end_to_end_with_everything_skipped(tmp_path: Path) -> None:
    bundle = tmp_path / "b"
    (bundle / "runtime" / "bin").mkdir(parents=True)
    (bundle / "runtime" / "bin" / "python3").symlink_to(sys.executable)
    shutil.copytree(_ROOT / "tools" / "rc1-tester" / "src", bundle / "tester" / "src")
    shutil.copy(_ROOT / "tools" / "rc1-tester" / "vmaf-tester-report", bundle / "tester")
    (bundle / "image").mkdir()
    (bundle / "image" / "fixtures.json").write_text('{"fixtures": []}')
    shutil.copy(RUN_SH, bundle / "run.sh")
    fake = tmp_path / "fake"
    fake.mkdir()
    script(fake / "uname", 'case "$1" in -s) echo Darwin;; -m) echo arm64;; esac\n')
    env = {**os.environ, "PATH": f"{fake}:{os.environ['PATH']}"}
    args = ["sh", str(bundle / "run.sh")]
    for check_name in ("dispatch", "metal", "unit", "golden"):
        args += ["--skip", check_name]
    result = subprocess.run(args, env=env, capture_output=True, text=True, check=False)
    report = json.loads(result.stdout)
    assert result.returncode in (1, 2) and "verdict" in result.stderr
    assert report["schema_version"] == "1"
    assert "sandbox-exec" in result.stderr
