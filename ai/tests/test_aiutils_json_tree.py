# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Positive, negative and boundary tests of ``aiutils.json_tree`` and its callers."""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any

import pytest

from aiutils import json_tree
from aiutils.json_tree import json_safe, map_json_tree
from aiutils.run_manifest import _describe_path_value, normalise_manifest_value


def _identity(value: Any) -> Any:
    return value


def test_map_copies_containers_and_keeps_key_order() -> None:
    source = {"b": [1, {"z": 1, "a": 2}], "a": 3}
    copy: Any = map_json_tree(source, _identity)
    assert copy == source
    assert list(copy) == ["b", "a"]
    assert list(copy["b"][1]) == ["z", "a"]
    assert copy is not source and copy["b"] is not source["b"]


def test_map_applies_leaf_to_every_non_container() -> None:
    assert map_json_tree([1, [2, {"k": 3}]], lambda v: v * 10) == [10, [20, {"k": 30}]]


def test_map_scalar_root_goes_through_leaf() -> None:
    assert map_json_tree(4, lambda v: v + 1) == 5


def test_map_empty_containers() -> None:
    assert map_json_tree({}, _identity) == {}
    assert map_json_tree([], _identity) == []


def test_map_deep_nesting_does_not_recurse() -> None:
    deep: Any = "leaf"
    for _ in range(5000):
        deep = [deep]
    out: Any = map_json_tree(deep, str.upper)
    for _ in range(5000):
        out = out[0]
    assert out == "LEAF"


def test_map_node_budget_is_enforced(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(json_tree, "NODE_LIMIT", 3)
    with pytest.raises(ValueError, match="too large"):
        map_json_tree([1, 2, 3, 4], _identity)


def test_map_sorted_keys_later_duplicate_wins() -> None:
    out: Any = map_json_tree({1: "a", "1": "b"}, _identity, key=str, sort_by=str)
    assert out == {"1": "b"}


def test_json_safe_maps_na_and_numpy_scalars() -> None:
    pytest.importorskip("pandas")
    np = pytest.importorskip("numpy")
    out: Any = json_safe({1: [math.nan, np.float64(2.5), {"x": np.int64(3), "y": None}]})
    assert out == {"1": [None, 2.5, {"x": 3, "y": None}]}
    assert type(out["1"][1]) is float


def test_normalise_manifest_value_sorts_and_stringifies() -> None:
    out: Any = normalise_manifest_value({"b": Path("p"), "a": (1, math.inf), 3: object.__name__})
    assert list(out) == ["3", "a", "b"]
    assert out["a"] == [1, None]
    assert out["b"] == "p"


def test_describe_path_value_walks_nested_sequences() -> None:
    root = Path(".").resolve()
    out: Any = _describe_path_value([Path("no-such-file"), [None, 7]], repo_root=root)
    assert out[0]["kind"] == "missing"
    assert out[1] == [None, 7]
