# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Reference scores baked into the tester image at build time.

`generate_reference()` runs in the image build, with the image's own binary
(under qemu when the hosted runner is x86). The report compares the scores of
the tester's machine against those files with `==`.
"""

from __future__ import annotations

import json
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

from .hw_equiv import SCALAR_CPUMASK, Runner, Scores, compare_scores, run_fixture, status_of
from .safe_process import run_bounded

REFERENCE_SCHEMA = 1
CROSS_ARCH_FILE = "x86_64-scalar.json"


def reference_name(machine: str, mode: str) -> str:
    """File name of the reference for an architecture and dispatch mode."""
    return f"{machine}-{mode}.json"


def generate_reference(
    vmaf: str,
    fixtures: Sequence[Mapping[str, Any]],
    out_dir: Path,
    *,
    machine: str,
    flags: Sequence[str],
    timeout_seconds: float,
    runner: Runner = run_bounded,
) -> list[Path]:
    """Write `<machine>-default.json` and `<machine>-scalar.json`; return both paths."""
    out_dir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for mode, cpumask in (("default", None), ("scalar", SCALAR_CPUMASK)):
        series = {
            str(fixture["id"]): run_fixture(
                vmaf, fixture, cpumask, timeout_seconds=timeout_seconds, runner=runner
            )
            for fixture in fixtures
        }
        document = {
            "schema": REFERENCE_SCHEMA,
            "machine": machine,
            "mode": mode,
            "dispatch_flags": sorted(flags) if mode == "default" else [],
            "fixtures": series,
        }
        target = out_dir / reference_name(machine, mode)
        target.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
        written.append(target)
    return written


def load_reference(path: Path) -> dict[str, Any] | None:
    """The parsed reference file, or None when it is absent or malformed."""
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    ok = (
        isinstance(document, dict)
        and document.get("schema") == REFERENCE_SCHEMA
        and isinstance(document.get("fixtures"), dict)
    )
    return document if ok else None


def compare_against(document: Mapping[str, Any], measured: Mapping[str, Scores]) -> dict[str, Any]:
    """Compare measured scores with one reference document, fixture by fixture."""
    cells: list[dict[str, Any]] = []
    for fixture_id, scores in measured.items():
        baked = document["fixtures"].get(fixture_id)
        cell: dict[str, Any] = {"fixture": fixture_id}
        if not isinstance(baked, dict):
            cell["error"] = "fixture missing from reference"
        else:
            cell.update(compare_scores(scores, baked))
        cells.append(cell)
    return {"status": status_of(cells), "fixtures": cells}


def _section(
    ref_dir: Path,
    name: str,
    measured: Mapping[str, Scores],
    *,
    gating: bool,
    host_flags: Sequence[str],
    mode: str,
) -> dict[str, Any]:
    document = load_reference(ref_dir / name)
    if document is None:
        return {"file": name, "status": "missing", "gating": gating, "fixtures": []}
    comparable = mode != "default" or sorted(document["dispatch_flags"]) == sorted(host_flags)
    result = compare_against(document, measured)
    return {
        "file": name,
        "generated_with_flags": document["dispatch_flags"],
        "comparable": comparable,
        "gating": gating and comparable,
        **result,
    }


def run_reference_equivalence(
    raw: Mapping[str, tuple[Scores, Scores]],
    ref_dir: Path,
    *,
    machine: str,
    host_flags: Sequence[str],
) -> dict[str, Any]:
    """Compare the dispatch run's scores with the baked references."""
    default = {fixture: pair[0] for fixture, pair in raw.items()}
    scalar = {fixture: pair[1] for fixture, pair in raw.items()}
    sections = {
        "own_arch_scalar": _section(
            ref_dir, reference_name(machine, "scalar"), scalar,
            gating=True, host_flags=host_flags, mode="scalar",
        ),
        "own_arch_default": _section(
            ref_dir, reference_name(machine, "default"), default,
            gating=True, host_flags=host_flags, mode="default",
        ),
    }  # fmt: skip
    if machine != "x86_64":
        sections["cross_arch_x86_scalar"] = _section(
            ref_dir, CROSS_ARCH_FILE, scalar, gating=False, host_flags=host_flags, mode="scalar"
        )
    gating = [section for section in sections.values() if section["gating"]]
    return {"status": overall_status(gating), **sections}


def overall_status(sections: Sequence[Mapping[str, Any]]) -> str:
    """`error` > `missing` > `differing` > `identical`; empty input is `missing`."""
    states = {section["status"] for section in sections}
    for state in ("error", "missing", "differing"):
        if state in states:
            return state
    return "identical" if states else "missing"
