#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every CUDA, SYCL and HIP twin names its features before it writes an option slot.

A twin's emitted names come from its feature-name dictionary, built from the
options it holds at that moment (core/test/feature_name_order.py). cambi.c and
the CPU extractors build it in init() first; a twin that writes an option slot
before (integer_vif_cuda cleared the no-op `enable_chroma`) emits names that
differ from what its caller derives from the same options, so the score is not
found under the expected name (T-GPU-VIF-NAMES-AFTER-OPTION-RESET-2026-10-05).

The Metal twins are covered by test_metal_twin_option_tables_contract.py
(T-METAL-CAMBI-SCORE-NAME-SUFFIXED-2026-10-05, #2132), which carries the same
walk inline; once both are on master, that file calls feature_name_order.py and
Metal joins BACKENDS here, so the walk has one implementation.

Device-free: reads the sources only.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import feature_name_order as order
import metal_option_tables as tables

FEATURE = tables.FEATURE
BACKENDS = ("cuda", "sycl", "hip")
SUFFIXES = (".c", ".cpp")
VIF_CUDA = "cuda/integer_vif_cuda.c"


def _sources() -> dict[str, str]:
    paths = [
        path
        for backend in BACKENDS
        for path in sorted((FEATURE / backend).rglob("*"))
        if path.suffix in SUFFIXES
    ]
    return {
        path.relative_to(FEATURE).as_posix(): path.read_text(encoding="utf-8") for path in paths
    }


def _failures(sources: dict[str, str]) -> list[str]:
    failures = []
    for name in sorted(sources):
        failures += order.init_order_failures(name, sources[name])
    return failures


class GpuTwinNameOrderContract(unittest.TestCase):
    def _edited(self, name: str, old: str, new: str) -> list[str]:
        sources = _sources()
        self.assertIn(old, sources[name])
        sources[name] = sources[name].replace(old, new, 1)
        return _failures(sources)

    def _assert_detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_every_twin_names_its_features_first(self) -> None:
        self.assertEqual(_failures(_sources()), [])

    def test_the_walk_reaches_every_backend(self) -> None:
        sources = _sources()
        for backend in BACKENDS:
            named = [
                n for n in sources if n.startswith(f"{backend}/") and order.NAME_DICT in sources[n]
            ]
            self.assertGreater(
                len(named), 5, f"{backend}: the walk read no twin that names features"
            )

    def test_vif_option_reset_before_names_is_detected(self) -> None:
        # The order on master before this fix: the no-op enable_chroma cleared
        # through a helper, then the names.
        failures = self._edited(
            VIF_CUDA,
            "    s->feature_name_dict =\n        vmaf_feature_name_dict_from_provided_features(",
            "    vif_drop_vestigial_chroma_option(s);\n"
            "    s->feature_name_dict =\n        vmaf_feature_name_dict_from_provided_features(",
        )
        self._assert_detected(
            failures, f"{VIF_CUDA}: init_fex_cuda writes option slot(s) ['enable_chroma']"
        )

    def test_direct_slot_write_before_names_is_detected(self) -> None:
        failures = self._edited(
            "sycl/integer_psnr_sycl.cpp",
            "    configure_geometry(s, pix_fmt, w, h);\n",
            "    s->enable_mse = false;\n    configure_geometry(s, pix_fmt, w, h);\n",
        )
        self._assert_detected(
            failures, "integer_psnr_sycl.cpp: init_fex_sycl writes option slot(s)"
        )

    def test_missing_names_are_detected(self) -> None:
        failures = self._edited(
            "hip/integer_psnr_hip.c", order.NAME_DICT, "vmaf_feature_names_elsewhere("
        )
        self._assert_detected(failures, "integer_psnr_hip.c: init_fex_hip builds no feature-name")

    def test_fixed_names_need_no_dictionary(self) -> None:
        # ssimulacra2 has no FEATURE_PARAM option and no dictionary: its names
        # never change.
        source = _sources()["cuda/ssimulacra2_cuda.c"]
        self.assertNotIn(order.NAME_DICT, source)
        self.assertEqual(order.init_order_failures("cuda/ssimulacra2_cuda.c", source), [])
        self.assertEqual(
            order.init_order_failures(
                "x.c", source.replace("};", "};\n/* VMAF_OPT_FLAG_FEATURE_PARAM */ int x;", 1)
            ),
            [],
            "a comment is not a FEATURE_PARAM option",
        )


if __name__ == "__main__":
    unittest.main()
