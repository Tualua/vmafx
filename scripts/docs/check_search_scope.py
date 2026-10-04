#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Check that the built site's search index covers user pages only (ADR-1512).

    python3 scripts/docs/check_search_scope.py build-docs/site

The search index (``search/search_index.json``, written by the search plugin)
must hold no entry for a record page: an ADR body (``adr/NNNN-*``), a research
digest, the rebase notes, the state ledger or the changelog archive. It must
hold the pages that keep record titles findable (the ADR index and title
list, the ADR tag pages, the research index and title list) and the user
pages. The exclusion itself is the ``search.exclude`` front matter that
``.meta.yml`` files apply per directory (Material's and Zensical's ``meta``
plugin); this check reads the result.

Exit status: 0 when the index matches, 1 on findings, 2 when the index cannot
be read.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Pages whose bodies stay out of the index, by location (URL path, no anchor).
EXCLUDED = (
    re.compile(r"^adr/\d{4}-[^/]+/$"),
    re.compile(r"^research/(?!titles/$)[^/]+/$"),
    re.compile(r"^rebase-notes/$"),
    re.compile(r"^state/$"),
    re.compile(r"^changelog-archive/.*$"),
)

# Pages that must have at least one entry.
REQUIRED = (
    "adr/",
    "adr/titles/",
    "adr/by-tag/",
    "research/",
    "research/titles/",
    "",
    "getting-started/",
    "usage/cli/",
)
REQUIRED_PREFIXES = ("adr/by-tag/",)


def page_of(location: str) -> str:
    """The page part of an index location: ``adr/0403-x/#context`` -> ``adr/0403-x/``."""
    return location.split("#", 1)[0]


def is_excluded(page: str) -> bool:
    return any(pattern.match(page) for pattern in EXCLUDED)


def findings(pages: set[str]) -> list[str]:
    out = [
        f"{page or '(landing page)'}: a record page is in the search index"
        for page in sorted(pages)
        if is_excluded(page)
    ]
    for page in REQUIRED:
        if page not in pages:
            out.append(f"{page or '(landing page)'}: missing from the search index")
    for prefix in REQUIRED_PREFIXES:
        if not any(page.startswith(prefix) and page != prefix for page in pages):
            out.append(f"{prefix}*: no tag page in the search index")
    return out


def load_pages(site: Path) -> set[str]:
    path = site / "search" / "search_index.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    return {page_of(entry["location"]) for entry in data["docs"]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("site", type=Path, help="built site directory")
    args = parser.parse_args(argv)
    try:
        pages = load_pages(args.site)
    except (OSError, ValueError, KeyError, TypeError) as err:
        print(f"check_search_scope: cannot read the search index: {err}", file=sys.stderr)
        return 2
    problems = findings(pages)
    for line in problems:
        print(f"check_search_scope: {line}", file=sys.stderr)
    if problems:
        return 1
    print(f"check_search_scope: {len(pages)} indexed pages, no record page among them")
    return 0


if __name__ == "__main__":
    sys.exit(main())
