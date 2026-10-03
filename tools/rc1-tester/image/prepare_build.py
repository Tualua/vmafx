#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Build-time helper of docker/Dockerfile.tester (never runs on the tester's machine).

select <build_dir> <unit-tests.txt>   print the build targets of the listed unit tests
stage  <build_dir> <unit-tests.txt> <image_root>
                                      copy the test executables, write unit-tests.json
info   <image_root>                   write image/build-info.json
fixtures <fixtures.sha256> <fixtures.json>
                                      print the manifest lines the report fixtures need
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

MIN_TESTS = 10


class BuildError(RuntimeError):
    """The build inputs are inconsistent; main() prints it and returns 1."""


def listed_names(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    return [line.strip() for line in lines if line.strip() and not line.startswith("#")]


def native_tests(build_dir: Path, names: list[str]) -> list[dict]:
    """Meson tests from the list that are executables of this build."""
    out = subprocess.run(
        ["meson", "introspect", "--tests", str(build_dir)],
        check=True, capture_output=True, text=True, timeout=120,
    ).stdout  # fmt: skip
    wanted = set(names)
    found = []
    for test in json.loads(out):
        command = Path(test["cmd"][0])
        if test["name"] in wanted and command.is_relative_to(build_dir):
            found.append({"name": test["name"], "path": command})
    if len(found) < MIN_TESTS:
        raise BuildError(f"only {len(found)} of the listed tests exist in this build")
    return sorted(found, key=lambda item: item["name"])


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def first_line(argv: list[str]) -> str:
    result = subprocess.run(argv, capture_output=True, text=True, timeout=60, check=False)
    return (result.stdout or result.stderr).strip().splitlines()[0]


def stage(build_dir: Path, names: list[str], image_root: Path) -> None:
    tests_dir = image_root / "tests"
    tests_dir.mkdir(parents=True, exist_ok=True)
    manifest = []
    for item in native_tests(build_dir, names):
        target = tests_dir / item["name"]
        shutil.copy2(item["path"], target)
        manifest.append({"name": item["name"], "cmd": str(target)})
    out = image_root / "image" / "unit-tests.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"tests": manifest}, indent=1) + "\n", encoding="utf-8")


def libc_description() -> str:
    if platform.system() == "Darwin":
        return "libSystem, macOS " + first_line(["sw_vers", "-productVersion"])
    return first_line(["ldd", "--version"])


def info(image_root: Path) -> None:
    libs = sorted((image_root / "build" / "src").glob("libvmaf.so.*.*.*"))
    data = {
        "source_commit": os.environ.get("VMAFX_SOURCE_COMMIT", "unknown"),
        "source_ref": os.environ.get("VMAFX_SOURCE_REF", "unknown"),
        "recipe_commit": os.environ.get("VMAFX_RECIPE_COMMIT", "unknown"),
        "built_by_workflow": os.environ.get("VMAFX_BUILT_BY_WORKFLOW", "false") == "true",
        "tag": os.environ.get("VMAFX_IMAGE_TAG", "unknown"),
        "base_image": os.environ.get("VMAFX_BASE_IMAGE", "unknown"),
        "image_arch": platform.machine().lower(),
        "compiler": first_line(["gcc", "--version"]),
        "libc": libc_description(),
        "meson": first_line(["meson", "--version"]),
        "vmaf_sha256": sha256(image_root / "build" / "tools" / "vmaf"),
        "libvmaf_sha256": sha256(libs[0]) if libs else None,  # None: libvmaf is static
        "kind": os.environ.get("VMAFX_ARTIFACT_KIND", "container-image"),
        "not_applicable": json.loads(os.environ.get("VMAFX_NOT_APPLICABLE", "{}")),
    }
    out = image_root / "image" / "build-info.json"
    out.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")


def report_fixture_lines(manifest: Path, fixtures: Path) -> list[str]:
    """Lines of the SHA-256 manifest for the files fixtures.json names."""
    names = set()
    for entry in json.loads(fixtures.read_text(encoding="utf-8"))["fixtures"]:
        for key in ("ref", "dis"):
            names.add(entry[key].removeprefix("python/test/resource/"))
    lines = [line for line in manifest.read_text(encoding="utf-8").splitlines() if line.strip()]
    chosen = [line for line in lines if line.split()[1] in names]
    if len(chosen) != len(names):
        raise BuildError("a fixture of fixtures.json is missing from the SHA-256 manifest")
    return chosen


def run(argv: list[str]) -> int:
    command = argv[1] if len(argv) > 1 else ""
    if command == "select" and len(argv) == 4:
        build_dir = Path(argv[2]).resolve()
        for item in native_tests(build_dir, listed_names(Path(argv[3]))):
            print(item["path"].relative_to(build_dir))
    elif command == "stage" and len(argv) == 5:
        stage(Path(argv[2]).resolve(), listed_names(Path(argv[3])), Path(argv[4]))
    elif command == "fixtures" and len(argv) == 4:
        print("\n".join(report_fixture_lines(Path(argv[2]), Path(argv[3]))))
    elif command == "info" and len(argv) == 3:
        info(Path(argv[2]))
    else:
        print(__doc__, file=sys.stderr)
        return 64
    return 0


def main(argv: list[str]) -> int:
    try:
        return run(argv)
    except BuildError as error:
        print(f"prepare_build: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
