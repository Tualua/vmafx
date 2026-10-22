#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Refuse a tag push that would bring a Netflix/vmaf tag or a stray name (ADR-1805).

Reads the ``git push`` hook protocol on stdin (``<local ref> <local sha>
<remote ref> <remote sha>`` per line). For every pushed ``refs/tags/*`` ref:

* a tag whose object or commit equals a tag Netflix/vmaf defines (the offline
  list in ``scripts/release/inherited-upstream-tags.json``) is refused;
* a name that matches none of the fork's tag patterns is refused.

Deleting a tag (all-zero local sha) is always allowed.
"""

from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path
from typing import Callable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.lib.safe_subprocess import run as run_command

INVENTORY = Path(__file__).resolve().parents[1] / "release" / "inherited-upstream-tags.json"
TAG_PREFIX = "refs/tags/"
ZERO = "0" * 40
PUSH_LINE_FIELDS = 4
TIMEOUT_SECONDS = 30
# Derived from the tags the fork creates: release-please (v1.0.0-rc.1, v1.0.0),
# the tester workflows (tester-<date>-<sha8>, tester-windows-<date>-<sha8>),
# archive/<topic> and tiny-blobs-v<N>. Netflix's v1.3.6rc and v3.0.0-rc do not match.
FORK_PATTERNS = (
    re.compile(r"v\d+\.\d+\.\d+(-rc\.\d+)?"),
    re.compile(r"tester-(windows-)?\d{8}-[0-9a-f]{8}"),
    re.compile(r"archive/[A-Za-z0-9._-]+"),
    re.compile(r"tiny-blobs-v\d+"),
)


def load_inventory(path: Path = INVENTORY) -> dict[str, set[str]]:
    """Map every Netflix tag name to the object and commit ids it points at."""
    data = json.loads(path.read_text(encoding="utf-8"))
    known: dict[str, set[str]] = {}
    for row in data["removed"] + data["upstream_only"]:
        known[row["name"]] = {row["object"], row["commit"]}
    return known


def peel(sha: str) -> str:
    """Return the commit a tag object points at (the id itself for a commit)."""
    git = shutil.which("git")
    if git is None:
        return sha
    result = run_command(
        [git, "rev-parse", "--verify", "--quiet", f"{sha}^{{commit}}"],
        allowed_executables=(git,),
        capture_output=True,
        text=True,
        timeout_seconds=TIMEOUT_SECONDS,
    )
    out = str(result.stdout).strip()
    return out if result.returncode == 0 and out else sha


def judge(name: str, sha: str, known: dict[str, set[str]], peeled: str) -> str | None:
    """Return the refusal reason for one pushed tag, or None when allowed."""
    if name in known and ({sha, peeled} & known[name]):
        return f"{name} is a Netflix/vmaf tag (same object); the fork does not carry them"
    if not any(pattern.fullmatch(name) for pattern in FORK_PATTERNS):
        return f"{name} matches none of the fork's tag patterns"
    return None


def check(
    lines: Sequence[str],
    known: dict[str, set[str]],
    peeler: Callable[[str], str] = peel,
) -> list[str]:
    """Judge every pushed tag line and collect the refusals."""
    problems: list[str] = []
    for line in lines:
        parts = line.split()
        if len(parts) != PUSH_LINE_FIELDS or not parts[2].startswith(TAG_PREFIX):
            continue
        sha = parts[1]
        if sha == ZERO:
            continue
        reason = judge(parts[2][len(TAG_PREFIX) :], sha, known, peeler(sha))
        if reason is not None:
            problems.append(reason)
    return problems


def main() -> int:
    problems = check(sys.stdin.read().splitlines(), load_inventory())
    for problem in problems:
        print(f"check-push-tags: refused: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
