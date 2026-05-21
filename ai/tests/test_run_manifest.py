# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Tests for shared AI run-manifest provenance helpers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from aiutils.run_manifest import (
    build_run_provenance,
    describe_path,
    normalise_namespace,
    write_manifest_json,
)


def test_describe_path_hashes_existing_files_relative_to_repo(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    root.mkdir()
    payload = root / "data.jsonl"
    payload.write_text("row\n", encoding="utf-8")

    described = describe_path(payload, repo_root=root)

    assert described["path"] == "data.jsonl"
    assert described["kind"] == "file"
    assert described["exists"] is True
    assert isinstance(described["sha256"], str)
    assert len(described["sha256"]) == 64


def test_normalise_namespace_serializes_paths_and_sorts_keys(tmp_path: Path) -> None:
    args = argparse.Namespace(
        beta=[tmp_path / "b", None],
        alpha=tmp_path / "a",
        hidden="skip",
    )

    normalised = normalise_namespace(args, exclude={"hidden"})

    assert list(normalised) == ["alpha", "beta"]
    assert normalised["alpha"] == str(tmp_path / "a")
    assert normalised["beta"] == [str(tmp_path / "b"), None]


def test_build_run_provenance_records_inputs_outputs_and_args(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    root.mkdir()
    script = root / "ai" / "scripts" / "train.py"
    script.parent.mkdir(parents=True)
    script.write_text("print('train')\n", encoding="utf-8")
    input_path = root / "features.jsonl"
    input_path.write_text("{}\n", encoding="utf-8")
    output_path = root / "model.onnx"

    provenance = build_run_provenance(
        entrypoint=script,
        repo_root=root,
        argv=["--features", str(input_path), "--out", str(output_path)],
        args=argparse.Namespace(features=input_path, out=output_path, run_argv_json="[]"),
        inputs={"features": input_path},
        outputs={"model": output_path},
        exclude_args={"run_argv_json"},
    )

    assert provenance["schema"] == "ai-run-provenance-v1"
    assert provenance["entrypoint"]["path"] == "ai/scripts/train.py"
    assert provenance["entrypoint"]["sha256"]
    assert provenance["args"] == {"features": str(input_path), "out": str(output_path)}
    assert provenance["inputs"]["features"]["kind"] == "file"
    assert provenance["outputs"]["model"]["kind"] == "missing"


def test_write_manifest_json_is_sorted_and_newline_terminated(tmp_path: Path) -> None:
    manifest = tmp_path / "manifest.json"

    write_manifest_json(manifest, {"z": 1, "a": {"b": 2}})

    raw = manifest.read_text(encoding="utf-8")
    assert raw.endswith("\n")
    assert raw.splitlines()[1].strip().startswith('"a"')
    assert json.loads(raw) == {"a": {"b": 2}, "z": 1}
