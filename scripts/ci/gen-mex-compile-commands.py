#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Add the MATLAB MEX sources to compile_commands.json for the `cpu` lane.

The twelve MEX sources under ``compat/python-vmaf/matlab/`` include ``mex.h`` and
``matrix.h`` from the MATLAB SDK, which exists on no runner or image, and meson
never builds them, so the lane could not measure them (T-TIDY-MATLAB-MEX-
UNMEASURED-2026-09-22). This script appends one entry per source whose include
path starts with the self-authored stubs in ``scripts/ci/lint-stubs/matlab/``.
The entries exist for clang-tidy only: no build runs them, nothing links the
stubs, and a MEX file compiled against them would not run.

Usage: gen-mex-compile-commands.py <build-dir>

Existing entries for the same files are replaced. A tree with no MEX source or
no stub header exits 1, so the lane cannot go clean by measuring nothing.
"""

from __future__ import annotations

import json
import shlex
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MEX_ROOT = Path("compat/python-vmaf/matlab")
STUB_DIR = Path("scripts/ci/lint-stubs/matlab")
STUB_HEADERS = ("mex.h", "matrix.h")
EXPECTED_ARGC = 2  # the script and the build directory


def mex_sources(repo_root: Path) -> list[Path]:
    """Every C source under the MATLAB harness, relative to *repo_root*."""
    return sorted(p.relative_to(repo_root) for p in (repo_root / MEX_ROOT).rglob("*.c"))


def entry_for(source: Path, build_dir: Path, repo_root: Path) -> dict[str, str]:
    """One compile-database entry for *source* (relative to *repo_root*)."""
    argv = [
        "cc",
        "-std=gnu11",
        f"-I{repo_root / STUB_DIR}",
        f"-I{(repo_root / source).parent}",
        "-c",
        str(repo_root / source),
        "-o",
        "/dev/null",
    ]
    return {
        "directory": str(build_dir),
        "command": shlex.join(argv),
        "file": str(repo_root / source),
    }


def merge(existing: list[dict[str, str]], added: list[dict[str, str]]) -> list[dict[str, str]]:
    """*existing* without the entries *added* replaces, then *added*."""
    replaced = {entry["file"] for entry in added}
    return [entry for entry in existing if entry.get("file") not in replaced] + added


def main(argv: list[str]) -> int:
    if len(argv) != EXPECTED_ARGC:
        print(f"usage: {argv[0]} <build-dir>", file=sys.stderr)
        return 2
    build_dir = Path(argv[1]).resolve()
    missing = [h for h in STUB_HEADERS if not (REPO_ROOT / STUB_DIR / h).is_file()]
    sources = mex_sources(REPO_ROOT)
    if missing or not sources:
        print(
            f"error: no MEX source or stub header ({len(sources)} sources, missing {missing})",
            file=sys.stderr,
        )
        return 1
    compdb = build_dir / "compile_commands.json"
    existing = json.loads(compdb.read_text(encoding="utf-8"))
    added = [entry_for(src, build_dir, REPO_ROOT) for src in sources]
    compdb.write_text(json.dumps(merge(existing, added), indent=2) + "\n", encoding="utf-8")
    print(f"gen-mex-compile-commands: {len(added)} MEX entries in {compdb}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
