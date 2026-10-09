#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Device-free contract of the second ADM viewing distance (ADR-2795).

- The C extractor (integer_adm.c) and its Rust twin (score.rs) file the second
  distance's scores under the same seven keys.
- adm_merge_view_dist() compares two contexts by their feature names, which
  carry only the options flagged VMAF_OPT_FLAG_FEATURE_PARAM, and checks the
  others itself. The table's options without the flag must stay exactly the
  ones it handles: debug, adm_skip_aim and adm_norm_view_dist_extra. A new
  option without the flag would be merged across silently.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
C_SRC = ROOT / "core/src/feature/integer_adm.c"
RUST_SCORE = ROOT / "core/src/rust/feature/adm/src/score.rs"
HANDLED_NON_PARAMS = {"debug", "adm_skip_aim", "adm_norm_view_dist_extra"}


def c_text() -> str:
    return C_SRC.read_text(encoding="utf-8")


def c_extra_keys(text: str) -> list[str]:
    suffix = re.search(r'#define VMAF_ADM_EXTRA_VIEW_KEY_SUFFIX "([^"]+)"', text)
    block = re.search(r"adm_extra_view_keys\[ADM_VIEW_SCORE_COUNT\] = \{(.*?)\};", text, re.S)
    if not suffix or not block:
        raise AssertionError("integer_adm.c: suffix or adm_extra_view_keys not found")
    bases = re.findall(r'"([^"]+)" VMAF_ADM_EXTRA_VIEW_KEY_SUFFIX', block.group(1))
    return [b + suffix.group(1) for b in bases]


def rust_extra_keys(text: str) -> list[str]:
    block = re.search(r"EXTRA_VIEW_NAMES: \[&CStr; 7\] = \[(.*?)\];", text, re.S)
    if not block:
        raise AssertionError("score.rs: EXTRA_VIEW_NAMES not found")
    return re.findall(r'c"([^"]+)"', block.group(1))


def option_entries(text: str) -> list[str]:
    table = re.search(r"static const VmafOption options\[\] = \{(.*?)\{0\}\};", text, re.S)
    if not table:
        raise AssertionError("integer_adm.c: option table not found")
    return re.split(r"\n    \{\n", table.group(1))[1:]


def non_feature_params(text: str) -> set[str]:
    names = set()
    for entry in option_entries(text):
        name = re.search(r'\.name = "([^"]+)"', entry)
        if name and "VMAF_OPT_FLAG_FEATURE_PARAM" not in entry:
            names.add(name.group(1))
    return names


class AdmViewDistContract(unittest.TestCase):
    def test_c_and_rust_file_under_the_same_keys(self) -> None:
        text = c_text()
        c_keys = c_extra_keys(text)
        self.assertEqual(len(c_keys), 7)
        self.assertEqual(c_keys, rust_extra_keys(RUST_SCORE.read_text(encoding="utf-8")))

    def test_merge_handles_every_option_outside_the_names(self) -> None:
        self.assertEqual(non_feature_params(c_text()), HANDLED_NON_PARAMS)

    def test_planted_unflagged_option_is_caught(self) -> None:
        planted = c_text().replace(
            "    {0}};",
            '    {\n        .name = "adm_planted",\n        .type = VMAF_OPT_TYPE_BOOL,\n    },\n    {0}};',
            1,
        )
        self.assertIn("adm_planted", non_feature_params(planted))

    def test_planted_key_drift_is_caught(self) -> None:
        drifted = c_text().replace('"integer_adm_scale3" VMAF_ADM_EXTRA_VIEW_KEY_SUFFIX', '"x"', 1)
        self.assertNotEqual(
            c_extra_keys(drifted), rust_extra_keys(RUST_SCORE.read_text(encoding="utf-8"))
        )


if __name__ == "__main__":
    sys.exit(unittest.main())
