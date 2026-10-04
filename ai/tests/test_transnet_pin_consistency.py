# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The TransNet V2 upstream pin is one commit across exporter, sidecar and registry.

The commit is the one that added the weights to ``soCzech/TransNetV2``; the
exporter's two SHA-256 constants equal the Git LFS object ids of that commit's
``saved_model.pb`` and ``variables.data-00000-of-00001`` pointer files.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
EXPORTER = REPO_ROOT / "ai" / "scripts" / "export_transnet_v2.py"
SIDECAR = REPO_ROOT / "model" / "tiny" / "transnet_v2.json"
REGISTRY = REPO_ROOT / "model" / "tiny" / "registry.json"

# Weights commit of soCzech/TransNetV2, verified 2026-10-04 against the LFS
# pointer oids of the two pinned hashes. The earlier pin (77498b8e...) does
# not exist upstream and answered 404.
WEIGHTS_COMMIT = "a0942ca347ee00aa455631147641954278b1d1a5"
DEAD_COMMIT_PREFIX = "77498b8e"


def _exporter_commit() -> str:
    match = re.search(r'^UPSTREAM_COMMIT = "([0-9a-f]{40})"', EXPORTER.read_text(), re.M)
    assert match is not None, "UPSTREAM_COMMIT constant missing"
    return match.group(1)


def test_exporter_pins_the_weights_commit() -> None:
    assert _exporter_commit() == WEIGHTS_COMMIT


def test_sidecar_matches_exporter() -> None:
    sidecar = json.loads(SIDECAR.read_text())
    assert sidecar["upstream_commit"] == _exporter_commit()
    assert _exporter_commit() in sidecar["license_url"]


def test_registry_matches_exporter() -> None:
    registry = json.loads(REGISTRY.read_text())
    entry = next(m for m in registry["models"] if m["id"] == "transnet_v2")
    assert _exporter_commit() in entry["license_url"]


def test_no_tracked_pin_refers_to_the_missing_commit() -> None:
    for path in (EXPORTER, SIDECAR, REGISTRY):
        assert DEAD_COMMIT_PREFIX not in path.read_text(), path
