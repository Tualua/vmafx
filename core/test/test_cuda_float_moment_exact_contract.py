#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin float_moment_cuda's sums to the CPU's terms (ADR-1453).

``moment.c::compute_2nd_moment()`` forms each sample's square in ``float`` and
adds the floats in ``double``. Up to 12 bits a square has at most 24
significant bits, so the float is the integer square. At 16 bits it is the
square rounded to 24 bits, and a twin that adds exact integer squares returns
another number: ``float_moment_cuda`` was up to 1.0e-4 from the CPU there.

The twin adds the float square, as an integer in units of 1 / scaler^2
(``moment_float_square()``). Every term is a multiple of the unit, so the
integer sum equals the CPU's double sum while that sum is below 2^53 units,
and the host recovers the moment with the CPU's two divisions.

Device-free: reads the sources only. ``test_cuda_float_moment_parity``
compares the scores on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CUDA_ROOT = ROOT / "core" / "src" / "feature" / "cuda"

HOST = "integer_moment_cuda.c"
KERNEL = "integer_moment/moment_score.cu"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

FLOAT_SQUARE = (
    "const float sample = (float)v;",
    "const float square = __fmul_rn(sample, sample);",
    "return (unsigned long long)square;",
)
WIDE_SQUARE = "return moment_float_square(v);"
THREAD_TERMS = ("m.v[2] += sample_square<T>(r);", "m.v[3] += sample_square<T>(d);")
WIDE_KERNEL = "thread_sums<uint16_t>(ref, dis, width, height)"
HOST_MOMENTS = (
    "const double ref2 = ((double)sums_host[2] / moment_scaler_sq) / n_pixels;",
    "const double dis2 = ((double)sums_host[3] / moment_scaler_sq) / n_pixels;",
)


def _flat(source: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", source).split())


def _sources() -> dict[str, str]:
    return {name: (CUDA_ROOT / name).read_text(encoding="utf-8") for name in (HOST, KERNEL)}


def _function_body(code: str, signature: str) -> str:
    """Flattened text of the definition that starts with `signature` (brace-matched), or empty."""
    start = code.find(signature)
    if start < 0:
        return ""
    depth = 0
    for index in range(code.index("{", start), len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[start : index + 1]
    return ""


def _kernel_failures(kernel: str) -> list[str]:
    code = _flat(kernel)
    failures: list[str] = []
    square = _function_body(code, "unsigned long long moment_float_square(unsigned long long v)")
    if any(piece not in square for piece in FLOAT_SQUARE):
        failures.append(f"{KERNEL}: moment_float_square() is not one fp32 product")
    if "double" in square:
        failures.append(f"{KERNEL}: moment_float_square() squares in fp64")
    wide = _function_body(code, "unsigned long long sample_square<uint16_t>(uint16_t v)")
    if WIDE_SQUARE not in wide:
        failures.append(f"{KERNEL}: a 16-bit sample does not contribute the CPU's float square")
    sums = _function_body(code, "MomentSums thread_sums(")
    if any(piece not in sums for piece in THREAD_TERMS):
        failures.append(f"{KERNEL}: thread_sums() does not add sample_square<T>()")
    if re.search(r"\+= [rd] \* [rd];", sums):
        failures.append(f"{KERNEL}: thread_sums() adds an exact integer square")
    if WIDE_KERNEL not in _function_body(code, "void calculate_moment_kernel_16bpc("):
        failures.append(f"{KERNEL}: the 16bpc kernel does not read uint16_t samples")
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


class FloatMomentCudaExactContract(unittest.TestCase):
    def _edited(self, name: str, old: str, new: str) -> list[str]:
        sources = _sources()
        self.assertIn(old, sources[name])
        sources[name] = sources[name].replace(old, new, 1)
        return _contract_failures(sources)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_integer_square_at_16_bits_is_detected(self) -> None:
        # The pre-ADR-1453 kernel: every sample type added r * r.
        failures = self._edited(
            KERNEL,
            "    return moment_float_square(v);",
            "    return (unsigned long long)v * (unsigned long long)v;",
        )
        self.assertTrue(any("CPU's float square" in item for item in failures), failures)

    def test_integer_square_in_the_thread_sum_is_detected(self) -> None:
        failures = self._edited(
            KERNEL, "            m.v[3] += sample_square<T>(d);", "            m.v[3] += d * d;"
        )
        self.assertTrue(any("sample_square<T>()" in item for item in failures), failures)
        self.assertTrue(any("exact integer square" in item for item in failures), failures)

    def test_double_square_is_detected(self) -> None:
        failures = self._edited(
            KERNEL,
            "    const float square = __fmul_rn(sample, sample);",
            "    const double square = (double)sample * (double)sample;",
        )
        self.assertTrue(any("one fp32 product" in item for item in failures), failures)
        self.assertTrue(any("fp64" in item for item in failures), failures)

    def test_plain_product_is_detected(self) -> None:
        # The rounding is an explicit intrinsic, as in every exact CUDA twin.
        failures = self._edited(
            KERNEL,
            "    const float square = __fmul_rn(sample, sample);",
            "    const float square = sample * sample;",
        )
        self.assertTrue(any("one fp32 product" in item for item in failures), failures)

    def test_host_division_in_another_order_is_detected(self) -> None:
        failures = self._edited(
            HOST,
            "    const double ref2 = ((double)sums_host[2] / moment_scaler_sq) / n_pixels;",
            "    const double ref2 = (double)sums_host[2] / (moment_scaler_sq * n_pixels);",
        )
        self.assertTrue(any("two divisions" in item for item in failures), failures)


if __name__ == "__main__":
    unittest.main()
