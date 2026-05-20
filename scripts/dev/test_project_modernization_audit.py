# Copyright 2026 Lusoris and Claude (Anthropic)
# SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
"""Tests for the project-modernization audit helper."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# pylint: disable=wrong-import-position
import project_modernization_audit as audit


def _write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def test_marker_scan_ranks_unblocked_stubs(tmp_path: Path) -> None:
    source = _write(
        tmp_path / "tools" / "vmaf-tune" / "src" / "vmaftune" / "thing.py",
        "# TODO: replace scaffold path\nraise NotImplementedError('stub')\n",
    )

    findings = audit.scan_marker_findings(tmp_path, [str(source.relative_to(tmp_path))])

    assert findings
    assert findings[0].path == str(source.relative_to(tmp_path))
    assert any(finding.kind == "not_implemented" for finding in findings)
    assert all(not finding.blocked for finding in findings)


def test_blocked_state_row_is_classified(tmp_path: Path) -> None:
    state = _write(
        tmp_path / ".workingdir2" / "OPEN.md",
        "- **T-HDR-VMAF-MODEL** — blocked on upstream Netflix releases.\n",
    )

    findings = audit.scan_state_files(tmp_path, [str(state.relative_to(tmp_path))])

    assert len(findings) == 1
    assert findings[0].blocked
    assert findings[0].blocked_reason.lower() == "blocked"


def test_smoke_model_registry_rows_are_reported(tmp_path: Path) -> None:
    registry = {
        "models": [
            {"id": "smoke_v0", "smoke": True},
            {"id": "fr_regressor_v1", "smoke": False},
        ]
    }
    _write(tmp_path / "model" / "tiny" / "registry.json", json.dumps(registry))

    findings = audit.scan_smoke_models(tmp_path)

    assert len(findings) == 1
    assert findings[0].kind == "smoke_model"
    assert "smoke_v0" in findings[0].evidence


def test_script_clusters_detect_large_one_off_families(tmp_path: Path) -> None:
    for idx in range(6):
        _write(tmp_path / "ai" / "scripts" / f"train_model_{idx}.py", "pass\n")

    clusters = audit.scan_script_clusters(tmp_path)

    assert len(clusters) == 1
    assert clusters[0].kind == "script_cluster"
    assert "6 ai/scripts/train_*.py" in clusters[0].evidence


def test_markdown_and_json_outputs(tmp_path: Path) -> None:
    _write(tmp_path / "ai" / "scripts" / "extract_gap.py", "# FIXME: deferred stub\n")
    report = audit.run_audit(
        tmp_path,
        roots=("ai",),
        state_files=(),
        max_per_file=3,
    )
    rendered = audit.render_markdown(report, max_findings=5)
    payload = report.to_json()

    assert "Project modernization audit" in rendered
    assert "Top Actionable Findings" in rendered
    assert payload["summary"]["total"] >= 1
    assert payload["findings"][0]["path"] == "ai/scripts/extract_gap.py"


def test_main_writes_reports(tmp_path: Path) -> None:
    _write(tmp_path / "docs" / "usage" / "feature.md", "Known limitations: scaffold.\n")
    out_json = tmp_path / "out" / "audit.json"
    out_md = tmp_path / "out" / "audit.md"

    rc = audit.main(
        [
            "--repo-root",
            str(tmp_path),
            "--scan-root",
            "docs/usage",
            "--state-file",
            "missing.md",
            "--out-json",
            str(out_json),
            "--out-md",
            str(out_md),
        ]
    )

    assert rc == 0
    assert json.loads(out_json.read_text(encoding="utf-8"))["summary"]["total"] >= 1
    assert "Known limitations" in out_md.read_text(encoding="utf-8")
