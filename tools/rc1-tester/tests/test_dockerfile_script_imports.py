# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every Dockerfile stage that runs a tester script has what the script imports.

`docker/Dockerfile.tester` copies only part of the repository into each build
stage. A script of `tools/rc1-tester/image/` that imports `vmaf_rc1_tester` runs
only in a stage that also holds `tools/rc1-tester/src`; without it the image
build dies with `ModuleNotFoundError`, which no other test sees because the
scripts run from a full checkout everywhere else.
"""

from __future__ import annotations

import ast
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DOCKERFILE = ROOT / "docker" / "Dockerfile.tester"
PACKAGE = "vmaf_rc1_tester"
PACKAGE_SOURCE = "tools/rc1-tester/src"
SCRIPT_RE = re.compile(r"python3\s+(?:/repo/|/src/)?(tools/rc1-tester/image/\w+\.py)")


def logical_lines(text: str) -> list[str]:
    """Join backslash continuations into one instruction line each."""
    lines: list[str] = []
    pending = ""
    for raw in text.splitlines():
        stripped = raw.strip()
        if not pending and stripped.startswith("#"):
            continue
        if stripped.endswith("\\"):
            pending += stripped[:-1] + " "
            continue
        lines.append(pending + stripped)
        pending = ""
    return lines


def parse_stages(text: str) -> dict[str, dict]:
    """Stage name -> parent stage, repository paths present, scripts run."""
    stages: dict[str, dict] = {}
    current: dict | None = None
    for line in logical_lines(text):
        head = re.match(r"FROM\s+(\S+)(?:\s+AS\s+(\S+))?", line, re.IGNORECASE)
        if head:
            current = {"parent": head.group(1), "paths": set(), "scripts": set()}
            stages[head.group(2) or head.group(1)] = current
            continue
        if current is None:
            continue
        if line.startswith("COPY ") and "--from=" not in line:
            current["paths"].update(t.rstrip("/") for t in line.split()[1:-1] if "=" not in t)
        for source in re.findall(r"--mount=type=bind,source=([^,\s]+)", line):
            if "from=" not in line.split(source)[0].split("--mount")[-1]:
                current["paths"].add(source.rstrip("/"))
        if line.startswith("RUN "):
            current["scripts"].update(SCRIPT_RE.findall(line))
    return stages


def holds(stages: dict[str, dict], name: str, path: str) -> bool:
    """True when the stage, or a stage it is built FROM, copies `path` or a parent of it."""
    seen: set[str] = set()
    while name in stages and name not in seen:
        seen.add(name)
        if any(path == p or path.startswith(p + "/") for p in stages[name]["paths"]):
            return True
        name = stages[name]["parent"]
    return False


def imports_package(script: Path) -> bool:
    tree = ast.parse(script.read_text(encoding="utf-8"))
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and (node.module or "").split(".")[0] == PACKAGE:
            return True
        if isinstance(node, ast.Import) and any(
            a.name.split(".")[0] == PACKAGE for a in node.names
        ):
            return True
    return False


def missing_imports(text: str) -> list[str]:
    stages = parse_stages(text)
    found = []
    for name, stage in stages.items():
        for script in sorted(stage["scripts"]):
            if imports_package(ROOT / script) and not holds(stages, name, PACKAGE_SOURCE):
                found.append(f"{name}: {script} imports {PACKAGE}, stage lacks {PACKAGE_SOURCE}")
    return found


def test_every_stage_has_the_package_its_scripts_import() -> None:
    assert missing_imports(DOCKERFILE.read_text(encoding="utf-8")) == []


def test_the_check_sees_the_build_stages_scripts() -> None:
    stages = parse_stages(DOCKERFILE.read_text(encoding="utf-8"))
    runners = {
        n for n, s in stages.items() if any(x.endswith("prepare_build.py") for x in s["scripts"])
    }
    assert {"vmaf-build", "sycl-build", "cuda-build", "hip-build"} <= runners


def test_a_stage_without_the_package_is_refused() -> None:
    dockerfile = (
        "FROM base AS build\n"
        "COPY tools/rc1-tester/image tools/rc1-tester/image\n"
        "RUN python3 tools/rc1-tester/image/prepare_build.py info /x\n"
    )
    assert len(missing_imports(dockerfile)) == 1
    fixed = dockerfile.replace("RUN", "COPY tools/rc1-tester/src tools/rc1-tester/src\nRUN")
    assert missing_imports(fixed) == []
