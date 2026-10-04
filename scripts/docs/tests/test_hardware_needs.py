#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The hardware-we-need table: status from reports, and the refusals of the generator."""

import importlib.util
import json
import sys
import unittest
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "generate_hardware_reports", ROOT / "scripts/docs/generate-hardware-reports.py"
)
assert SPEC is not None and SPEC.loader is not None
GEN = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GEN
SPEC.loader.exec_module(GEN)


def host(**kw: Any) -> dict[str, Any]:
    base = {
        "platform": "linux",
        "machine": "x86_64",
        "dispatch_flags": ["avx2", "avx512"],
        "cpu_model": "Intel(R) Xeon(R) Gold 6338",
    }
    return {"host": {**base, **kw}, "verdict": "pass"}


def gpu_report(
    family: str, status: str = "pass", rows: str = "pass", backend: str = "cuda"
) -> dict[str, Any]:
    device = {"facts": {"family": family}, "status": status, "rows": {"status": rows}}
    return {**host(), "gpu": {"backend": backend, "devices": [device]}}


def row(family_part: str) -> dict[str, Any]:
    needs: dict[str, Any] = json.loads(GEN.NEEDS.read_text(encoding="utf-8"))
    found: dict[str, Any] = next(r for r in needs["rows"] if family_part in r["family"])
    return found


class StatusTests(unittest.TestCase):
    def test_no_report(self) -> None:
        self.assertEqual(GEN._status(row("Hopper"), []), "no report yet")

    def test_cpu_report_counts_for_matching_row_only(self) -> None:
        reports = [host()]
        self.assertIn("1 reported, worst pass", GEN._status(row("Intel with AVX-512"), reports))
        self.assertEqual(
            GEN._status(row("AMD with AVX-512"), reports), "covered by Ryzen 9 9950X3D (Zen 5)"
        )
        self.assertEqual(GEN._status(row("Arm with SVE2"), reports), "no report yet")

    def test_windows_report_counts_for_its_architecture_and_the_cpu_rows(self) -> None:
        reports = [host(platform="windows")]
        self.assertIn("1 reported", GEN._status(row("Windows on x64"), reports))
        self.assertEqual(GEN._status(row("Windows on Arm"), reports), "no report yet")
        self.assertIn("1 reported", GEN._status(row("Intel with AVX-512"), reports))
        self.assertEqual(GEN._status(row("Windows on x64"), [host()]), "no report yet")
        arm = [host(platform="windows", machine="aarch64", dispatch_flags=["neon"])]
        self.assertIn("1 reported", GEN._status(row("Windows on Arm"), arm))

    def test_gpu_report_counts_for_its_family_and_a_fail_wins(self) -> None:
        reports = [gpu_report("hopper"), gpu_report("hopper", rows="fail")]
        self.assertIn("2 reported, worst fail", GEN._status(row("Hopper"), reports))
        self.assertEqual(GEN._status(row("Blackwell"), reports), "no report yet")
        self.assertEqual(
            GEN._status(row("Hopper"), [gpu_report("hopper", backend="hip")]), "no report yet"
        )

    def test_unmeasured_rows_are_not_a_pass(self) -> None:
        status = GEN._status(row("Hopper"), [gpu_report("hopper", rows="not_measured")])
        self.assertIn("not fully measured", status)


class RefusalTests(unittest.TestCase):
    def needs(self) -> dict[str, Any]:
        needs: dict[str, Any] = json.loads(GEN.NEEDS.read_text(encoding="utf-8"))
        return needs

    def test_shipped_file_is_consistent(self) -> None:
        self.assertEqual(GEN.needs_problems(self.needs()), [])

    def test_missing_gpu_family_is_refused(self) -> None:
        needs = self.needs()
        needs["rows"] = [r for r in needs["rows"] if "Hopper" not in r["family"]]
        self.assertIn(
            "cuda family hopper has no row in hardware-needs.json", GEN.needs_problems(needs)
        )

    def test_unknown_state_id_is_refused(self) -> None:
        needs = self.needs()
        needs["rows"][0]["state"] = ["T-NO-SUCH-ROW-2026-01-01"]
        self.assertIn(
            "state id T-NO-SUCH-ROW-2026-01-01 is not in docs/state.md", GEN.needs_problems(needs)
        )

    def test_splice_needs_markers(self) -> None:
        with self.assertRaises(ValueError):
            GEN.splice("no markers", "table")
        out = GEN.splice(f"a\n{GEN.BEGIN}\nold\n{GEN.END}\nz", "T\n")
        self.assertIn("T\n", out)
        self.assertNotIn("old", out)


if __name__ == "__main__":
    unittest.main()
