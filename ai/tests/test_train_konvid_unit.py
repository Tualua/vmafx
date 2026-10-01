# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Characterisation tests for :mod:`ai.scripts.train_konvid`."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
from typing import Any

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
_SCRIPT = _REPO_ROOT / "ai" / "scripts" / "train_konvid.py"


def _load_module() -> Any:
    spec = importlib.util.spec_from_file_location("train_konvid_under_test", _SCRIPT)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


TK: Any = _load_module()


class _MockDataset:
    def __init__(self, n: int = 10, keys: list[str] | None = None) -> None:
        self.n = n
        self.keys = keys

    def __len__(self) -> int:
        return self.n

    def __getitem__(self, idx: int) -> int:
        return idx


def test_split_dataset_without_keys() -> None:
    ds = _MockDataset(n=20, keys=None)
    tr, va, te = TK._split_dataset(ds, val_frac=0.2, test_frac=0.2, seed=42)
    assert len(tr) + len(va) + len(te) == 20
    assert len(va) == 4
    assert len(te) == 4
    assert len(tr) == 12


def test_split_dataset_with_keys() -> None:
    keys = [f"k{i % 4}" for i in range(20)]
    ds = _MockDataset(n=20, keys=keys)
    tr, va, te = TK._split_dataset(ds, val_frac=0.25, test_frac=0.25, seed=42)
    assert len(tr) + len(va) + len(te) == 20


def test_train_c2_missing_parquet(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setattr(TK, "C2_PARQUET", tmp_path / "nonexistent.parquet")
    args = argparse.Namespace()
    with pytest.raises((SystemExit, FileNotFoundError)):
        TK.train_c2(args)


def test_train_c3_missing_parquet(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setattr(TK, "C3_PARQUET", tmp_path / "nonexistent.parquet")
    args = argparse.Namespace()
    with pytest.raises((SystemExit, FileNotFoundError)):
        TK.train_c3(args)
