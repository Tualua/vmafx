# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Shared helper utilities for AI scripts.

This package centralizes common patterns (file hashing, time formatting,
CLI setup, Parquet I/O) to reduce code duplication across the ai/scripts/
directory and establish standard interfaces for new scripts.
"""

from aiutils.file_utils import sha256
from aiutils.jsonl_utils import iter_jsonl
from aiutils.run_manifest import (
    build_run_manifest_payload,
    build_run_provenance,
    describe_path,
    write_manifest_json,
    write_run_manifest,
)
from aiutils.time_utils import now_iso_8601

__all__ = [
    "build_run_manifest_payload",
    "build_run_provenance",
    "describe_path",
    "iter_jsonl",
    "now_iso_8601",
    "sha256",
    "write_manifest_json",
    "write_parquet_atomic",
    "write_run_manifest",
]


def __getattr__(name: str):
    """Import optional heavy helpers only when the caller asks for them."""
    if name == "write_parquet_atomic":
        from aiutils.parquet_utils import write_parquet_atomic

        return write_parquet_atomic
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
