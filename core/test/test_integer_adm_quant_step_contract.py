#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin upstream's exponent in every copy of the integer ADM quantisation step (ADR-1475).

``dwt_quant_step()`` raises 10 to ``k * temp * temp``. Upstream Netflix/vmaf
(``libvmaf/src/feature/integer_adm.c``) forms that product in ``float``. The
fork widened it to ``double`` in a static-analysis sweep (PR #552), which moved
every integer ADM score, and went back to upstream's form in ADR-1475.

Three sources carry the expression: the CPU's
``core/src/feature/integer_adm_kernels.h`` (which the CUDA and HIP hosts
include) and the two twins that hold their own copy, SYCL and Metal. This test
reads them: a copy that promotes an operand of the product would make its twin
differ from the CPU in the last digits, and the Metal copy cannot be run on a
Linux runner at all.

Device-free: reads the sources only. ``test_integer_adm_quant_step`` checks the
CPU's values.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE_ROOT = ROOT / "core" / "src" / "feature"

# file -> name of its quantisation-step function
COPIES = {
    "integer_adm_kernels.h": "dwt_quant_step",
    "sycl/integer_adm_sycl.cpp": "dwt_quant_step",
    "metal/integer_adm_metal.mm": "iadm_dwt_quant_step",
}

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
FLOAT_PRODUCT = re.compile(r"params->k\s*\*\s*temp\s*\*\s*temp")
# An operand of the product promoted before the multiplication.
WIDENED_OPERAND = re.compile(r"\(\s*double\s*\)\s*(?:temp\b|params->k\b)")


def _sources() -> dict[str, str]:
    return {name: (FEATURE_ROOT / name).read_text(encoding="utf-8") for name in COPIES}


def _function_body(text: str, name: str) -> str | None:
    """Body of the definition of ``name``, comments removed; None when absent."""
    code = COMMENT.sub("", text)
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", code)
    if match is None:
        return None
    depth = 1
    end = match.end()
    while depth > 0 and end < len(code):
        depth += {"{": 1, "}": -1}.get(code[end], 0)
        end += 1
    return code[match.end() : end]


def _contract_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    for name, function in COPIES.items():
        body = _function_body(sources[name], function)
        if body is None:
            failures.append(f"{name}: no definition of {function}()")
            continue
        if not FLOAT_PRODUCT.search(body):
            failures.append(f"{name}: {function}() does not form params->k * temp * temp")
        if WIDENED_OPERAND.search(body):
            failures.append(f"{name}: {function}() promotes an operand of the exponent to double")
    return failures


class IntegerAdmQuantStepContract(unittest.TestCase):
    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_widened_operand_is_detected_in_every_copy(self) -> None:
        # The form PR #552 introduced, planted into each copy in turn.
        for name in COPIES:
            with self.subTest(name=name):
                sources = _sources()
                planted, count = FLOAT_PRODUCT.subn(
                    "params->k * (double)temp * temp", sources[name]
                )
                self.assertGreater(count, 0)
                sources[name] = planted
                failures = _contract_failures(sources)
                self.assertTrue(any(name in item and "promotes" in item for item in failures))

    def test_metal_spelling_of_the_widened_product_is_detected(self) -> None:
        sources = _sources()
        name = "metal/integer_adm_metal.mm"
        sources[name] = FLOAT_PRODUCT.sub("(double)params->k * (double)temp * temp", sources[name])
        failures = _contract_failures(sources)
        self.assertTrue(any(name in item and "promotes" in item for item in failures))

    def test_missing_copy_is_detected(self) -> None:
        sources = _sources()
        name = "sycl/integer_adm_sycl.cpp"
        sources[name] = sources[name].replace("dwt_quant_step", "renamed_quant_step")
        failures = _contract_failures(sources)
        self.assertTrue(any("no definition of dwt_quant_step()" in item for item in failures))

    def test_a_cast_in_a_comment_is_not_a_finding(self) -> None:
        sources = _sources()
        name = "integer_adm_kernels.h"
        sources[name] = sources[name].replace(
            "static inline float dwt_quant_step(",
            "/* not `(double)temp` */\nstatic inline float dwt_quant_step(",
        )
        self.assertEqual(_contract_failures(sources), [])


if __name__ == "__main__":
    unittest.main()
