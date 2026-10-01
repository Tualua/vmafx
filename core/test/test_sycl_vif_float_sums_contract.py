#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the host tail of vif_sycl to the CPU's.

``integer_vif.c`` accumulates in int64 and then leaves integer arithmetic:
``vif_store_residuals()`` stores each scale's numerator and denominator in a
``float``, ``write_scores()`` adds those rounded values for the debug outputs,
and the shared emitter divides in single precision
(``single_precision_ratio``). The SYCL kernels accumulate in int64 too, so
the twin can only return the CPU's values when its host tail rounds at the
same three points. It kept the sums in ``double`` and was up to 3.5e-7 from
the CPU on every frame.

The twin's ``debug`` option also has to default to the CPU's ``false``:
with ``true`` it published eleven outputs the CPU extractor does not.

Device-free: reads the sources only. ``test_sycl_vif_parity`` compares the
scores on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE_ROOT = ROOT / "core" / "src" / "feature"

TWIN = "sycl/integer_vif_sycl.cpp"
CPU = "integer_vif.c"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
SPACE = re.compile(r"\s+")


def _code(source: str) -> str:
    """The source without comments and with whitespace collapsed."""
    return SPACE.sub(" ", COMMENT.sub(" ", source))


def _sources() -> dict[str, str]:
    return {name: (FEATURE_ROOT / name).read_text(encoding="utf-8") for name in (TWIN, CPU)}


def _function_body(source: str, name: str) -> str:
    """Text of the first definition of `name` (brace-matched), or empty."""
    match = re.search(rf"\b{name}\([^;{{]*\)\s*\{{", source)
    if not match:
        return ""
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    return ""


def _debug_option(code: str) -> str:
    """The option-table entry named "debug": from its name to the next entry."""
    start = code.find('.name = "debug",')
    if start < 0:
        return ""
    end = code.find(".name = ", start + 1)
    return code[start : end if end >= 0 else len(code)]


def _cpu_failures(sources: dict[str, str]) -> list[str]:
    """The CPU properties the twin mirrors; a change here needs the twin changed."""
    cpu = _code(sources[CPU])
    failures: list[str] = []
    store = _function_body(cpu, "vif_store_residuals")
    if "(const VifResiduals *acc, float *num, float *den)" not in store:
        failures.append(f"{CPU}: vif_store_residuals() no longer stores the sums in float")
    if ".single_precision_ratio = true," not in _function_body(cpu, "write_scores"):
        failures.append(f"{CPU}: write_scores() no longer asks for a single-precision ratio")
    if ".default_val.b = false," not in _debug_option(cpu):
        failures.append(f"{CPU}: the debug option no longer defaults to false")
    return failures


def _twin_failures(sources: dict[str, str]) -> list[str]:
    twin = _code(sources[TWIN])
    failures: list[str] = []
    sums = _function_body(twin, "vif_scale_sums")
    if "(const struct vif_accums *accums, float *vif_scale_num, float *vif_scale_den)" not in sums:
        failures.append(f"{TWIN}: vif_scale_sums() must store each scale's sums in float")
    if sums.count("(float)(") != 2:
        failures.append(f"{TWIN}: each scale's numerator and denominator is rounded to float once")
    score_set = _function_body(twin, "vif_score_set")
    for piece in (
        ".single_precision_ratio = true,",
        "output.score_num += vif_scale_num[scale];",
        "output.score_den += vif_scale_den[scale];",
        "output.score = output.score_den > 0.0 ? output.score_num / output.score_den : NAN;",
    ):
        if piece not in score_set:
            failures.append(f"{TWIN}: vif_score_set() is not integer_vif.c::write_scores() ({piece})")
    if "const float *vif_scale_num, const float *vif_scale_den" not in score_set:
        failures.append(f"{TWIN}: the frame sums must add the float-rounded per-scale values")
    if ".default_val = {.b = false}," not in _debug_option(twin):
        failures.append(f"{TWIN}: the debug option must default to false, as on the CPU")
    return failures


def _failures(sources: dict[str, str]) -> list[str]:
    return _cpu_failures(sources) + _twin_failures(sources)


class SyclVifFloatSumsContractTest(unittest.TestCase):
    def _edited(self, old: str, new: str) -> list[str]:
        sources = _sources()
        self.assertIn(old, sources[TWIN])
        sources[TWIN] = sources[TWIN].replace(old, new, 1)
        return _failures(sources)

    def test_live_sources_round_where_the_cpu_does(self) -> None:
        self.assertEqual(_failures(_sources()), [])

    def test_double_scale_sums_are_detected(self) -> None:
        failures = self._edited(
            "static inline void vif_scale_sums(const struct vif_accums *accums, "
            "float *vif_scale_num,\n                                  float *vif_scale_den)",
            "static inline void vif_scale_sums(const struct vif_accums *accums, "
            "double *vif_scale_num,\n                                  double *vif_scale_den)",
        )
        self.assertTrue(any("sums in float" in failure for failure in failures), failures)

    def test_double_precision_ratio_is_detected(self) -> None:
        failures = self._edited("        .single_precision_ratio = true,\n", "")
        self.assertTrue(any("single_precision_ratio" in failure for failure in failures), failures)

    def test_debug_default_true_is_detected(self) -> None:
        failures = self._edited(".default_val = {.b = false},", ".default_val = {.b = true},")
        self.assertTrue(any("debug option must default" in failure for failure in failures), failures)

    def test_unrounded_frame_sum_is_detected(self) -> None:
        failures = self._edited(
            "            output.score_num += vif_scale_num[scale];",
            "            output.score_num += exact_num[scale];",
        )
        self.assertTrue(any("write_scores()" in failure for failure in failures), failures)


if __name__ == "__main__":
    unittest.main()
