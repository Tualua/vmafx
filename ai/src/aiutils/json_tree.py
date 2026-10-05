# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Copy a nested JSON-like value with a per-leaf transform, without recursion.

The manifest, JSONL and sidecar writers all walk a document of dicts and lists
and rewrite its leaves (replace a non-finite float, render a ``Path``, turn a
numpy scalar into a Python one). One explicit-stack walk serves them all
(HISS-01, HISS-19); the node budget bounds the loop (HISS-02).
"""

from __future__ import annotations

import importlib
from collections.abc import Callable, Iterable, Mapping
from typing import Any

# Upper bound on the nodes of one document; a larger one raises ValueError.
NODE_LIMIT = 10_000_000


def _is_dict(value: Any) -> bool:
    return isinstance(value, dict)


def _is_list(value: Any) -> bool:
    return isinstance(value, list)


def map_json_tree(
    value: Any,
    leaf: Callable[[Any], Any],
    *,
    is_mapping: Callable[[Any], bool] = _is_dict,
    is_sequence: Callable[[Any], bool] = _is_list,
    key: Callable[[Any], Any] | None = None,
    sort_by: Callable[[Any], Any] | None = None,
) -> Any:
    """Return a copy of ``value`` whose non-container nodes went through ``leaf``.

    A node for which ``is_mapping`` holds becomes a ``dict`` (keys through ``key``
    when given; items visited in ``sort_by(key)`` order when given), a node for
    which ``is_sequence`` holds becomes a ``list``; every other node is replaced
    by ``leaf(node)``. The containers are copied, so ``value`` is not modified,
    and the key order of the result is the order of the source (or ``sort_by``).
    """
    result: list[Any] = [None]
    pending: list[tuple[Any, Any, Any]] = [(value, result, 0)]
    for _ in range(NODE_LIMIT):
        if not pending:
            return result[0]
        node, holder, slot = pending.pop()
        if is_mapping(node):
            holder[slot] = _open_mapping(node, pending, key, sort_by)
        elif is_sequence(node):
            items: list[Any] = list(node)
            copy_list: list[Any] = [None] * len(items)
            holder[slot] = copy_list
            pending.extend((child, copy_list, index) for index, child in enumerate(items))
        else:
            holder[slot] = leaf(node)
    raise ValueError("document is too large to walk")


def _open_mapping(
    node: Mapping[Any, Any],
    pending: list[tuple[Any, Any, Any]],
    key: Callable[[Any], Any] | None,
    sort_by: Callable[[Any], Any] | None,
) -> dict[Any, Any]:
    """Create the empty copy of a mapping (keys placed in order) and queue its children."""
    pairs: Iterable[tuple[Any, Any]] = node.items()
    if sort_by is not None:
        pairs = sorted(pairs, key=lambda pair: sort_by(pair[0]))
    copy_dict: dict[Any, Any] = {}
    children: list[tuple[Any, Any, Any]] = []
    for source_key, child in pairs:
        new_key = source_key if key is None else key(source_key)
        copy_dict[new_key] = None
        children.append((child, copy_dict, new_key))
    # Reversed: children are popped in source order, so a later duplicate key wins.
    pending.extend(reversed(children))
    return copy_dict


def _pandas_safe_leaf(value: Any) -> Any:
    pd = importlib.import_module("pandas")
    if pd.isna(value):
        return None
    if hasattr(value, "item"):
        try:
            return value.item()
        except ValueError:
            return value
    return value


def json_safe(value: Any) -> Any:
    """Return ``value`` with dict keys as ``str``, NA scalars as ``None`` and numpy scalars unboxed.

    Used for DataFrame records on their way to ``json.dumps``; needs pandas.
    """
    return map_json_tree(value, _pandas_safe_leaf, key=str)
