#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Fail when an ADR is still `Proposed` although implementing commits landed long ago.

A status line that says `Proposed` while the code it decides has been on master
for weeks misleads readers about what is in force (96 ADRs had drifted this way
by 2026-10-05). This check finds every ADR whose status is `Proposed` and whose
number is cited in the subject of a commit that

  * is reachable from the chosen ref,
  * is at least `--min-age-days` old (default 14), and
  * has an implementing Conventional Commit type (`feat`, `fix`, `perf`,
    `refactor`, `build`, `ci`, `test`; a `docs`, `plan`, `design` or `chore`
    subject only records or schedules a decision).

The fix is to accept (or supersede) the ADR with a dated `### Status update`
note. An ADR whose decision is only partly shipped may stay Proposed through one
entry in `scripts/ci/adr-status-exceptions.json`: ADR number, file, rule, a
reason naming the missing part, and an expiry date. The check fails on

  * a drifted ADR without an exception,
  * an exception past its expiry date (valid through the date itself),
  * an exception that no longer matches a drifted ADR (stale; delete it), and
  * a malformed or duplicate entry.

Exit 0 only when none of those hold.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RULE = "proposed-with-implementing-commit"
EXCEPTIONS_FILE = ROOT / "scripts" / "ci" / "adr-status-exceptions.json"
IMPLEMENTING = {"feat", "fix", "perf", "refactor", "build", "ci", "test"}
STATUS_RE = re.compile(r"^-\s+\*\*Status\*\*:\s*(\w+)", re.IGNORECASE)
SUBJECT_TYPE_RE = re.compile(r"^(\w+)(?:\([^)]*\))?!?:")
CITE_RE = re.compile(r"ADR-0*(\d+)\b")
STATUS_SCAN_LINES = 15
GIT_TIMEOUT_S = 120
# A hook or a worktree exports these; they would point `git -C <repo>` at another repository.
FOREIGN_GIT_ENV = ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR")


def proposed_adrs(adr_dir: Path) -> dict[int, Path]:
    """Map ADR number to file for every ADR whose status line starts `Proposed`."""
    found: dict[int, Path] = {}
    for path in sorted(adr_dir.glob("[0-9][0-9][0-9][0-9]-*.md")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for line in lines[:STATUS_SCAN_LINES]:
            match = STATUS_RE.match(line)
            if match:
                if match.group(1).lower() == "proposed":
                    found[int(path.name[:4])] = path
                break
    return found


def implementing_citations(
    repo: Path, ref: str, cutoff: datetime
) -> dict[int, list[tuple[str, str]]]:
    """Map ADR number to (short sha, subject) of old implementing commits citing it."""
    git = shutil.which("git")
    if git is None:
        raise RuntimeError("git is not on PATH")
    env = {k: v for k, v in os.environ.items() if k not in FOREIGN_GIT_ENV}
    out = subprocess.run(  # noqa: S603 - fixed argv; ref comes from the operator's own command line
        [git, "-C", str(repo), "log", ref, "--format=%h%x01%ct%x01%s"],
        check=True,
        capture_output=True,
        text=True,
        timeout=GIT_TIMEOUT_S,
        env=env,
    ).stdout
    cites: dict[int, list[tuple[str, str]]] = {}
    for row in out.splitlines():
        sha, stamp, subject = row.split("\x01", 2)
        kind = SUBJECT_TYPE_RE.match(subject)
        if kind is None or kind.group(1) not in IMPLEMENTING:
            continue
        if datetime.fromtimestamp(int(stamp), tz=timezone.utc) > cutoff:
            continue
        for number in {int(n) for n in CITE_RE.findall(subject)}:
            cites.setdefault(number, []).append((sha, subject))
    return cites


def drift(
    repo: Path, ref: str, min_age_days: int, now: datetime | None = None
) -> list[tuple[int, Path, list[tuple[str, str]]]]:
    """Return (number, file, commits) for each Proposed ADR with old implementing commits."""
    cutoff = (now or datetime.now(timezone.utc)) - timedelta(days=min_age_days)
    proposed = proposed_adrs(repo / "docs" / "adr")
    cites = implementing_citations(repo, ref, cutoff)
    return [(n, proposed[n], cites[n]) for n in sorted(proposed) if n in cites]


def load_exceptions(path: Path, repo: Path) -> tuple[dict[int, dict[str, str]], list[str]]:
    """Return (number -> entry, problems) from the exception list."""
    problems: list[str] = []
    entries: dict[int, dict[str, str]] = {}
    if not path.is_file():
        return entries, []
    raw = json.loads(path.read_text(encoding="utf-8"))
    for item in raw.get("exceptions", []):
        number = item.get("adr")
        fields = ("file", "rule", "reason", "expires")
        if not isinstance(number, int) or any(not str(item.get(k, "")).strip() for k in fields):
            problems.append(f"malformed exception entry (needs adr, {', '.join(fields)}): {item}")
            continue
        if item["rule"] != RULE:
            problems.append(f"ADR-{number:04d}: exception rule must be {RULE!r}")
        elif not (repo / item["file"]).is_file() or not item["file"].startswith(
            f"docs/adr/{number:04d}-"
        ):
            problems.append(f"ADR-{number:04d}: exception file {item['file']!r} is not that ADR")
        elif number in entries:
            problems.append(f"ADR-{number:04d}: duplicate exception entry")
        else:
            try:
                date.fromisoformat(item["expires"])
            except ValueError:
                problems.append(f"ADR-{number:04d}: expires {item['expires']!r} is not YYYY-MM-DD")
                continue
            entries[number] = item
    return entries, problems


def evaluate(
    repo: Path,
    ref: str,
    min_age_days: int,
    exceptions_path: Path,
    today: date,
    now: datetime | None = None,
) -> tuple[list[str], list[tuple[int, Path, list[tuple[str, str]]]]]:
    """Return (problems, drifted rows)."""
    rows = drift(repo, ref, min_age_days, now=now)
    entries, problems = load_exceptions(exceptions_path, repo)
    drifted = {n for n, _, _ in rows}
    for number, path, _ in rows:
        entry = entries.get(number)
        if entry is None:
            problems.append(
                f"ADR-{number:04d} is Proposed ({path.relative_to(repo)}) but implementing "
                "commits landed: accept or supersede it, or add an exception"
            )
        elif today > date.fromisoformat(entry["expires"]):
            problems.append(f"ADR-{number:04d}: exception expired {entry['expires']}")
    for number in sorted(set(entries) - drifted):
        problems.append(f"ADR-{number:04d}: exception is stale (ADR no longer drifts); delete it")
    return problems, rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--repo", type=Path, default=ROOT)
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--min-age-days", type=int, default=14)
    parser.add_argument("--exceptions", type=Path, default=EXCEPTIONS_FILE)
    parser.add_argument("--today", type=date.fromisoformat, default=None, help="YYYY-MM-DD")
    args = parser.parse_args(argv)
    today = args.today or datetime.now(timezone.utc).date()
    problems, rows = evaluate(args.repo, args.ref, args.min_age_days, args.exceptions, today)
    for message in problems:
        print(f"check-adr-status-drift: {message}")
    print(
        f"check-adr-status-drift: {len(rows)} drifted Proposed ADR(s), {len(problems)} problem(s)"
    )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
