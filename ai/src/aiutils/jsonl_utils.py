# Copyright 2026 Lusoris
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent OR MIT
"""JSONL file I/O utilities."""

from __future__ import annotations

import json
from collections.abc import Mapping
from pathlib import Path
from typing import Any, Iterator

from aiutils.run_manifest import normalise_manifest_value


def iter_jsonl(path: Path) -> Iterator[tuple[int, dict]]:
    """Yield (line_no, row) tuples from a JSONL file. Skips blank lines.

    Args:
        path: Path to a JSONL file (newline-delimited JSON objects).

    Yields:
        Tuple of (line_no, parsed_dict) for each non-blank line.
        line_no is 1-indexed.

    Raises:
        SystemExit: If a non-blank line contains invalid JSON.
    """
    with path.open("r", encoding="utf-8") as fp:
        for line_no, line in enumerate(fp, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                yield line_no, json.loads(stripped)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"error: {path}:{line_no}: invalid JSON ({exc})") from exc


def dumps_jsonl_row(
    row: Mapping[str, Any],
    *,
    sort_keys: bool = True,
    separators: tuple[str, str] | None = None,
) -> str:
    """Serialize one strict JSONL object row with a trailing newline."""
    normalised = normalise_manifest_value(row)
    return (
        json.dumps(
            normalised,
            sort_keys=sort_keys,
            allow_nan=False,
            separators=separators,
        )
        + "\n"
    )
