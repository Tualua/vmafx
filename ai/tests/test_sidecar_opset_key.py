# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""One sidecar key names the ONNX opset: ``opset``.

The registry (``registry.schema.json``), the registry validator, the C loader
(``core/src/dnn/model_loader.c``) and every shipped sidecar use ``opset``;
``onnx_opset`` was a second spelling that only some writers produced and the
validator never read.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TINY = REPO_ROOT / "model" / "tiny"
LOADER = REPO_ROOT / "core" / "src" / "dnn" / "model_loader.c"
SIDECARS = sorted(
    p
    for p in TINY.glob("*.json")
    if p.name not in {"registry.json", "registry.schema.json"}
    and "PROMOTE" not in p.name
    and "schema_version" not in json.loads(p.read_text())
)


def test_sidecars_were_found() -> None:
    assert len(SIDECARS) >= 20


def test_sidecars_use_opset_not_onnx_opset() -> None:
    legacy = []
    malformed = []
    for sidecar in SIDECARS:
        doc = json.loads(sidecar.read_text())
        if "onnx_opset" in doc:
            legacy.append(sidecar.name)
        if "opset" in doc and not isinstance(doc["opset"], int):
            malformed.append(sidecar.name)
    assert legacy == []
    assert malformed == []


def test_sidecar_opset_matches_registry() -> None:
    registry = {m["id"]: m for m in json.loads((TINY / "registry.json").read_text())["models"]}
    checked = 0
    for sidecar in SIDECARS:
        entry = registry.get(sidecar.stem)
        if entry is None or "opset" not in entry:
            continue
        assert json.loads(sidecar.read_text())["opset"] == entry["opset"], sidecar.name
        checked += 1
    assert checked >= 10


def test_c_loader_reads_the_same_key() -> None:
    src = LOADER.read_text()
    assert re.search(r'extract_int\(buf, "opset", &out->opset\)', src)
    assert "onnx_opset" not in src


def test_python_registry_writer_emits_opset() -> None:
    from vmaf_train.registry import ModelMetadata

    assert "opset" in ModelMetadata.model_fields
    assert "onnx_opset" not in ModelMetadata.model_fields
