#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Integer VIF's residual variance has one defined definition (ADR-1561).

Netflix's ``integer_vif.c`` writes::

    int32_t sv_sq = sigma2_sq - g * sigma12;
    sv_sq = (uint32_t)(MAX(sv_sq, 0));

The fp64 difference reaches about -2^45, and converting a value outside
``int32_t``'s range is undefined behaviour (C11 6.3.1.4). x86 returns
INT32_MIN for it, so the clamp gives 0; an aarch64 build that vectorises the
loop keeps the low 32 bits of a 64-bit conversion instead. ``vif_sv_sq()`` in
``core/src/feature/integer_vif_sv_sq.h`` returns x86's value for every input,
and every path that mirrors the statistic calls it: the scalar statistic, the
AVX2 and NEON per-pixel helpers, the CUDA and HIP kernels and the SYCL math
test's reference. The AVX-512 vector path converts through 64-bit lanes, which
is defined for the whole range, and clamps; the SYCL kernel forms the value in
integers (ADR-1432). The Metal kernel on master forms the term in fp32 and is
no mirror of the fp64 expression.

Device-free: reads the sources. ``test_integer_vif_sv_sq`` checks the values,
and under clang's ``-fsanitize=undefined`` (the sanitizer job) it fails on the
undefined conversion.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE = ROOT / "core" / "src" / "feature"
TESTS = ROOT / "core" / "test"
HELPER = FEATURE / "integer_vif_sv_sq.h"
CALL = "vif_sv_sq(sigma2_sq, g, sigma12)"
MIRRORS = (
    FEATURE / "integer_vif.c",
    FEATURE / "x86" / "vif_avx2.c",
    FEATURE / "arm64" / "vif_neon.c",
    FEATURE / "cuda" / "integer_vif" / "vif_statistics.cuh",
    FEATURE / "hip" / "integer_vif" / "vif_statistics.hip",
    TESTS / "test_sycl_integer_vif_math.c",
)
HELPER_BODY = (
    "VMAF_IVIF_FUNC uint32_t vif_sv_sq(int32_t sigma2_sq, double g, int32_t sigma12) {",
    "const double sv = sigma2_sq - g * sigma12;",
    "return (sv > 0.0 && sv < 2147483648.0) ? (uint32_t)sv : 0u;",
)
SUFFIXES = {".c", ".h", ".cpp", ".cu", ".cuh", ".hip", ".mm", ".metal"}

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
SPACE = re.compile(r"\s+")
DIFFERENCE = r"\(?\s*sigma2_sq\s*-\s*g\s*\*\s*sigma12\s*\)?"
INT_CAST = r"(?:\(\s*(?:int|int32_t)\s*\)\s*)?"
# An integer declared from the raw difference: the scalar, AVX2, NEON and HIP
# spellings of upstream's line.
DECLARED = re.compile(rf"\b(?:int|int32_t)\s+sv_sq\s*=\s*{INT_CAST}{DIFFERENCE}\s*;")
# An assignment of the raw difference to an integer declared elsewhere: the
# CUDA spelling, flagged when the file declares sv_sq as an integer.
ASSIGNED = re.compile(rf"(?<![\w.])sv_sq\s*=\s*{INT_CAST}{DIFFERENCE}\s*;")
INT_DECL = re.compile(r"\b(?:int|int32_t)\s+sv_sq\b")


def code(source: str) -> str:
    """The source without comments, whitespace collapsed."""
    return SPACE.sub(" ", COMMENT.sub(" ", source))


def undefined_conversions(source: str) -> list[str]:
    """Statements that convert the fp64 difference to a signed integer."""
    text = code(source)
    found = [m.group(0) for m in DECLARED.finditer(text)]
    if INT_DECL.search(text):
        found += [m.group(0) for m in ASSIGNED.finditer(text)]
    return found


def scanned_files() -> list[Path]:
    files = [p for p in FEATURE.rglob("*") if p.suffix in SUFFIXES and p.is_file()]
    files += [p for p in TESTS.glob("*") if p.suffix in SUFFIXES and p.is_file()]
    return sorted(files)


class ScannerTest(unittest.TestCase):
    """The scanner flags upstream's spellings and passes the fp32 ones."""

    def test_flags_the_upstream_spellings(self) -> None:
        planted = {
            "scalar": "double g = s; int32_t sv_sq = sigma2_sq - g * sigma12; sv_sq = 0;",
            "hip": "int32_t sv_sq = (int32_t)(sigma2_sq - g * sigma12);",
            "cuda": "int32_t sv_sq = sigma2_sq; /* x */ sv_sq = sigma2_sq - g * sigma12;",
        }
        for name, source in planted.items():
            with self.subTest(name):
                self.assertTrue(undefined_conversions(source), name)

    def test_passes_float_terms_and_the_helper(self) -> None:
        for source in (
            "float sv_sq = sigma2_sq - g * sigma12;",
            "uint32_t sv_sq = vif_sv_sq(sigma2_sq, g, sigma12);",
            "const double sv = sigma2_sq - g * sigma12;",
            "/* int32_t sv_sq = sigma2_sq - g * sigma12; */ uint32_t sv_sq = 0;",
        ):
            with self.subTest(source):
                self.assertEqual(undefined_conversions(source), [])


class SvSqContractTest(unittest.TestCase):
    def test_helper_is_the_defined_form(self) -> None:
        body = code(HELPER.read_text(encoding="utf-8"))
        position = 0
        for line in HELPER_BODY:
            found = body.find(line, position)
            self.assertGreaterEqual(found, 0, f"integer_vif_sv_sq.h lost `{line}`")
            position = found

    def test_every_mirror_calls_the_helper(self) -> None:
        for path in MIRRORS:
            with self.subTest(path.name):
                text = code(path.read_text(encoding="utf-8"))
                self.assertTrue('integer_vif_sv_sq.h"' in text, f"{path.name} does not include it")
                self.assertTrue(CALL in text, f"{path.name} does not call {CALL}")

    def test_no_integer_conversion_of_the_difference(self) -> None:
        failures = []
        for path in scanned_files():
            for statement in undefined_conversions(path.read_text(encoding="utf-8")):
                failures.append(f"{path.relative_to(ROOT)}: {statement}")
        self.assertEqual(failures, [], "convert through vif_sv_sq() (ADR-1561)")

    def test_avx512_converts_through_64_bit_lanes(self) -> None:
        text = code((FEATURE / "x86" / "vif_avx512.c").read_text(encoding="utf-8"))
        for piece in (
            "__m512i msv_sq = _mm512_cvttpd_epi64(",
            "msv_sq = _mm512_max_epi64(msv_sq, _mm512_setzero_si512());",
        ):
            self.assertTrue(piece in text, f"vif_avx512.c lost `{piece}`")


if __name__ == "__main__":
    unittest.main()
