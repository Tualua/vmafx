# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Shared JSON helpers for vmaf-tune report-style payloads."""

from __future__ import annotations

import json
import math
from typing import Any


def nan_to_none(value: Any) -> Any:
    """Recursively substitute ``None`` for non-finite floats."""
    if isinstance(value, float):
        return None if math.isnan(value) or math.isinf(value) else value
    if isinstance(value, dict):
        return {k: nan_to_none(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [nan_to_none(v) for v in value]
    return value


def dumps_strict(data: Any, *, indent: int | None = 2, sort_keys: bool = True) -> str:
    """Dump portable RFC-8259 JSON with non-finite floats represented as null."""
    return json.dumps(nan_to_none(data), indent=indent, sort_keys=sort_keys, allow_nan=False)


__all__ = ["dumps_strict", "nan_to_none"]
