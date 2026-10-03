# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The Intel GPU image's row map stays in step with the ledger, the tests and the gate.

`tools/rc1-tester/image/sycl-rows.json` names, per state row of docs/state.md
and device family, the device tests, audits and gate cells that close it. This
keeps it honest without a device: every row exists in docs/state.md and every
test it names is spelled out in that row, every test is a Meson test of the SYCL
build's `gpu` suite, every family is one hw_l0probe.py knows, every audit is one
the SYCL backend parses, and every gate feature is a feature of the parity gate.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "rc1-tester" / "src"))
sys.path.insert(0, str(ROOT))

from scripts.ci.cross_backend_parity_gate import FEATURE_METRICS

from vmaf_rc1_tester.hw_l0probe import FAMILIES
from vmaf_rc1_tester.hw_sycl import SYCL

ROWS = json.loads((ROOT / "tools" / "rc1-tester" / "image" / "sycl-rows.json").read_text())
STATE = (ROOT / "docs" / "state.md").read_text(encoding="utf-8")
MESON = (ROOT / "core" / "test" / "meson.build").read_text(encoding="utf-8")


def state_row(row_id: str) -> str:
    match = re.search(r"^\| \*\*" + re.escape(row_id) + r"\*\* \|.*$", STATE, re.MULTILINE)
    assert match, f"{row_id} is not a row of docs/state.md"
    return match.group(0)


def meson_suites(test: str) -> str:
    match = re.search(r"test\('" + re.escape(test) + r"',.*?\)\n", MESON, re.DOTALL)
    assert match, f"{test} is not a Meson test of core/test/meson.build"
    return match.group(0)


def test_rows_exist_and_spell_out_their_tests() -> None:
    for row in ROWS["rows"]:
        text = state_row(row["id"])
        for test in row["tests"]:
            assert test in text, f"{row['id']} does not name {test}"


def test_named_tests_are_gpu_suite_tests() -> None:
    for row in ROWS["rows"]:
        for test in row["tests"] + row.get("audits", []):
            assert "'gpu'" in meson_suites(test), f"{test} is not in the gpu suite"


def test_families_audits_and_gate_features_are_known() -> None:
    families = {family for *_, family in FAMILIES}
    for row in ROWS["rows"]:
        assert set(row["families"]) <= families
        assert set(row.get("audits", [])) <= set(SYCL.audits)
        assert {spec["feature"] for spec in row.get("gate", [])} <= set(FEATURE_METRICS)


def test_every_family_of_the_default_aot_list_has_a_row() -> None:
    covered = {family for row in ROWS["rows"] for family in row["families"]}
    assert {"xe-lp", "xe-lpg", "xe-hpg", "xe2"} <= covered
