#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin float_moment_sycl's sums to the CPU's terms (ADR-1449).

``moment.c::compute_2nd_moment()`` forms each sample's square in ``float`` and
adds the floats in ``double``. Up to 12 bits a square has at most 24
significant bits, so the float is the integer square. At 16 bits it is the
square rounded to 24 bits, and a twin that adds exact integer squares returns
another number: ``float_moment_sycl`` was up to 1.0e-4 from the CPU there.

The twin adds the float square, as an integer in units of 1 / scaler^2
(``moment_float_square()``). Every term is a multiple of the unit, so the
integer sum equals the CPU's double sum while that sum is below 2^53 units,
and the host recovers the moment with the CPU's two divisions.

Device-free: reads the sources only. ``test_sycl_float_moment_parity``
compares the scores on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TWIN = ROOT / "core" / "src" / "feature" / "sycl" / "integer_moment_sycl.cpp"
REFERENCE = ROOT / "core" / "src" / "feature" / "moment.c"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

FLOAT_SQUARE = (
    "const auto sample = (float)v;",
    "const float square = sample * sample;",
    "return (int64_t)square;",
)
ALWAYS_INLINED = "VMAF_SYCL_ALWAYS_INLINE int64_t moment_float_square(uint32_t v)"
KERNEL_TERMS = (
    "atomic64(e_sums[2]).fetch_add(moment_float_square(r));",
    "atomic64(e_sums[3]).fetch_add(moment_float_square(d));",
)
HOST_MOMENTS = (
    "const double ref2 = ((double)s->h_sums[2] / moment_scaler_sq) / n_pixels;",
    "const double dis2 = ((double)s->h_sums[3] / moment_scaler_sq) / n_pixels;",
)
# The CPU's term and its sum, which the kernel and the host mirror.
REFERENCE_LINES = (
    "const float term = pic_ * pic_;",
    "cum += (double)term;",
    "cum /= ((double)w * h);",
)


def _flat(source: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", source).split())


def _sources() -> dict[str, str]:
    return {
        "twin": TWIN.read_text(encoding="utf-8"),
        "reference": REFERENCE.read_text(encoding="utf-8"),
    }


def _function_body(code: str, name: str) -> str:
    """Flattened text of the definition of `name` (brace-matched), or empty."""
    match = re.search(rf"\b{name}\([^;{{]*\) \{{", code)
    if not match:
        return ""
    depth = 0
    for index in range(match.end() - 1, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[match.start() : index + 1]
    return ""


def _kernel_failures(twin: str) -> list[str]:
    code = _flat(twin)
    failures: list[str] = []
    square = _function_body(code, "moment_float_square")
    if any(piece not in square for piece in FLOAT_SQUARE):
        failures.append("moment_float_square() is not one fp32 product")
    if "double" in square:
        failures.append("moment_float_square() squares in fp64")
    if ALWAYS_INLINED not in code:
        failures.append("moment_float_square() is not always inlined into the kernel")
    kernel = _function_body(code, "launch_moment")
    if any(piece not in kernel for piece in KERNEL_TERMS):
        failures.append("the kernel does not add the CPU's float squares")
    if re.search(r"fetch_add\([rd] \* [rd]\)", kernel):
        failures.append("the kernel adds an exact integer square")
    return failures


def _host_failures(twin: str) -> list[str]:
    code = _flat(twin)
    return [
        f"a second moment is not the CPU's two divisions of the integer sum ({piece})"
        for piece in HOST_MOMENTS
        if piece not in code
    ]


def _reference_failures(reference: str) -> list[str]:
    code = _flat(reference)
    return [
        f"moment.c no longer holds `{line}`; the twin mirrors it"
        for line in REFERENCE_LINES
        if line not in code
    ]


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return (
        _kernel_failures(sources["twin"])
        + _host_failures(sources["twin"])
        + _reference_failures(sources["reference"])
    )


class FloatMomentSyclExactContract(unittest.TestCase):
    def _edited(self, name: str, old: str, new: str) -> list[str]:
        sources = _sources()
        self.assertIn(old, sources[name])
        sources[name] = sources[name].replace(old, new, 1)
        return _contract_failures(sources)

    def _assert_detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_integer_square_is_detected(self) -> None:
        # The pre-ADR-1449 kernel.
        failures = self._edited(
            "twin",
            "atomic64(e_sums[2]).fetch_add(moment_float_square(r));",
            "atomic64(e_sums[2]).fetch_add(r * r);",
        )
        self._assert_detected(failures, "CPU's float squares")
        self._assert_detected(failures, "exact integer square")

    def test_distorted_plane_left_exact_is_detected(self) -> None:
        failures = self._edited(
            "twin",
            "atomic64(e_sums[3]).fetch_add(moment_float_square(d));",
            "atomic64(e_sums[3]).fetch_add(d * d);",
        )
        self._assert_detected(failures, "exact integer square")

    def test_double_square_is_detected(self) -> None:
        failures = self._edited(
            "twin",
            "    const float square = sample * sample;",
            "    const double square = (double)sample * (double)sample;",
        )
        self._assert_detected(failures, "one fp32 product")
        self._assert_detected(failures, "fp64")

    def test_plain_inline_is_detected(self) -> None:
        # A call left in a kernel is a scratch-memory frame (ADR-1395).
        failures = self._edited(
            "twin",
            "VMAF_SYCL_ALWAYS_INLINE int64_t moment_float_square(uint32_t v)",
            "inline int64_t moment_float_square(uint32_t v)",
        )
        self._assert_detected(failures, "always inlined")

    def test_host_division_in_another_order_is_detected(self) -> None:
        failures = self._edited(
            "twin",
            "    const double ref2 = ((double)s->h_sums[2] / moment_scaler_sq) / n_pixels;",
            "    const double ref2 = (double)s->h_sums[2] / (moment_scaler_sq * n_pixels);",
        )
        self._assert_detected(failures, "two divisions")

    def test_changed_reference_is_detected(self) -> None:
        failures = self._edited(
            "reference",
            "const float term = pic_ * pic_;",
            "const double term = (double)pic_ * pic_;",
        )
        self._assert_detected(failures, "the twin mirrors it")


if __name__ == "__main__":
    unittest.main()
