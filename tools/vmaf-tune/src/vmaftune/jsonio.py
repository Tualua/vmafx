# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Shared JSON helpers for vmaf-tune report-style payloads."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

_NODE_LIMIT = 10_000_000


def _finite_or_none(value: Any) -> Any:
    if isinstance(value, float) and (math.isnan(value) or math.isinf(value)):
        return None
    return value


def nan_to_none(value: Any) -> Any:
    """Substitute ``None`` for non-finite floats; tuples become lists, containers are copied.

    Walks with an explicit work stack instead of recursing (HISS-01); a document of
    more than ``_NODE_LIMIT`` nodes raises ``ValueError``.
    """
    result: list[Any] = [None]
    pending: list[tuple[Any, Any, Any]] = [(value, result, 0)]
    for _ in range(_NODE_LIMIT):
        if not pending:
            return result[0]
        node, holder, slot = pending.pop()
        if isinstance(node, dict):
            copy_dict: dict[Any, Any] = {key: None for key in node}
            holder[slot] = copy_dict
            pending.extend((child, copy_dict, key) for key, child in node.items())
        elif isinstance(node, (list, tuple)):
            copy_list: list[Any] = [None] * len(node)
            holder[slot] = copy_list
            pending.extend((child, copy_list, index) for index, child in enumerate(node))
        else:
            holder[slot] = _finite_or_none(node)
    raise ValueError("document is too large to sanitise")


def dumps_strict(data: Any, *, indent: int | None = 2, sort_keys: bool = True) -> str:
    """Dump portable RFC-8259 JSON with non-finite floats represented as null."""
    return json.dumps(nan_to_none(data), indent=indent, sort_keys=sort_keys, allow_nan=False)


def write_json_strict(
    path: Path,
    data: Any,
    *,
    indent: int | None = 2,
    sort_keys: bool = True,
    trailing_newline: bool = True,
) -> None:
    """Atomically write portable RFC-8259 JSON to ``path``."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    payload = dumps_strict(data, indent=indent, sort_keys=sort_keys)
    if trailing_newline:
        payload += "\n"
    tmp.write_text(payload, encoding="utf-8")
    tmp.replace(path)


__all__ = ["dumps_strict", "nan_to_none", "write_json_strict"]
