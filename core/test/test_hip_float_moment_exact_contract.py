#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin float_moment_hip's sums to the CPU's terms (ADR-1447).

``moment.c::compute_2nd_moment()`` forms each sample's square in ``float`` and
adds the floats in ``double``. Up to 12 bits a square has at most 24
significant bits, so the float is the integer square. At 16 bits it is the
square rounded to 24 bits, and a twin that adds exact integer squares returns
another number: ``float_moment_hip`` was up to 1.0e-4 from the CPU there.

The twin adds the float square, as an integer in units of 1 / scaler^2
(``moment_float_square()``). Every term is a multiple of the unit, so the
integer sum equals the CPU's double sum while that sum is below 2^53 units,
and the host recovers the moment with the CPU's two divisions.

Device-free: reads the sources only. ``test_hip_float_moment_parity`` compares
the scores on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HIP_ROOT = ROOT / "core" / "src" / "feature" / "hip"

HOST = "float_moment_hip.c"
KERNEL = "float_moment/moment_score.hip"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

FLOAT_SQUARE = (
    "const float sample = (float)v;",
    "const float square = sample * sample;",
    "return (uint64_t)square;",
)
WIDE_TERMS = ("r2 = moment_float_square(r);", "d2 = moment_float_square(d);")
HOST_MOMENTS = (
    "const double ref2nd = ((double)sums[2] / moment_scaler_sq) / n_pix;",
    "const double dis2nd = ((double)sums[3] / moment_scaler_sq) / n_pix;",
)


def _flat(source: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", source).split())


def _sources() -> dict[str, str]:
    return {name: (HIP_ROOT / name).read_text(encoding="utf-8") for name in (HOST, KERNEL)}


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


def _kernel_failures(kernel: str) -> list[str]:
    code = _flat(kernel)
    failures: list[str] = []
    square = _function_body(code, "moment_float_square")
    if any(piece not in square for piece in FLOAT_SQUARE):
        failures.append(f"{KERNEL}: moment_float_square() is not one fp32 product")
    if "double" in square:
        failures.append(f"{KERNEL}: moment_float_square() squares in fp64")
    wide = _function_body(code, "calculate_moment_hip_kernel_16bpc")
    if any(piece not in wide for piece in WIDE_TERMS):
        failures.append(f"{KERNEL}: the 16-bit kernel does not add the CPU's float squares")
    if re.search(r"\b[rd]2 = [rd] \* [rd];", wide):
        failures.append(f"{KERNEL}: the 16-bit kernel adds an exact integer square")
    return failures


def _host_failures(host: str) -> list[str]:
    code = _flat(host)
    return [
        f"{HOST}: a second moment is not the CPU's two divisions of the integer sum ({piece})"
        for piece in HOST_MOMENTS
        if piece not in code
    ]


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return _kernel_failures(sources[KERNEL]) + _host_failures(sources[HOST])


class FloatMomentHipExactContract(unittest.TestCase):
    def _edited(self, name: str, old: str, new: str) -> list[str]:
        sources = _sources()
        self.assertIn(old, sources[name])
        sources[name] = sources[name].replace(old, new, 1)
        return _contract_failures(sources)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_integer_square_at_16_bits_is_detected(self) -> None:
        # The pre-ADR-1447 kernel.
        failures = self._edited(
            KERNEL, "        r2 = moment_float_square(r);", "        r2 = r * r;"
        )
        self.assertTrue(any("CPU's float squares" in item for item in failures), failures)
        self.assertTrue(any("exact integer square" in item for item in failures), failures)

    def test_distorted_plane_left_exact_is_detected(self) -> None:
        failures = self._edited(
            KERNEL, "        d2 = moment_float_square(d);", "        d2 = d * d;"
        )
        self.assertTrue(any("exact integer square" in item for item in failures), failures)

    def test_double_square_is_detected(self) -> None:
        failures = self._edited(
            KERNEL,
            "    const float square = sample * sample;",
            "    const double square = (double)sample * (double)sample;",
        )
        self.assertTrue(any("one fp32 product" in item for item in failures), failures)
        self.assertTrue(any("fp64" in item for item in failures), failures)

    def test_host_division_in_another_order_is_detected(self) -> None:
        failures = self._edited(
            HOST,
            "    const double ref2nd = ((double)sums[2] / moment_scaler_sq) / n_pix;",
            "    const double ref2nd = (double)sums[2] / (moment_scaler_sq * n_pix);",
        )
        self.assertTrue(any("two divisions" in item for item in failures), failures)


if __name__ == "__main__":
    unittest.main()
