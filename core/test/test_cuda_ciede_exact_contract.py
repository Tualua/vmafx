#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the CPU-arithmetic ciede_cuda design (ADR-1426).

The CPU extractor (``ciede.c``) computes in double and stores in float, and a
twin has to copy where it does which:

- every math function takes a double there, because C promotes a float
  argument; the kernel is C++, which would pick the float overload, so the
  promotion is written out;
- the squares are fp64 products of floats, exact;
- ``powf`` appears with a float result in two places and nowhere else;
- the per-pixel value is a float and the frame sum one double that takes the
  values in raster order.

Device-free: reads the sources only. Every planted regression below is a
construct the pre-ADR-1426 twin had, so the contract fails on the old design
and passes on the new one. ``test_ciede_device_math`` replays the shared
header against the CPU extractor on the host, and ``test_cuda_ciede_parity``
bounds the device's math library on a device; this contract keeps the design
from eroding on hosts without one.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CUDA_ROOT = ROOT / "core" / "src" / "feature" / "cuda"

HOST = "integer_ciede_cuda.c"
KERNEL = "integer_ciede/ciede_score.cu"
DEVICE = "integer_ciede/ciede_device.h"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
# A float math function: the pre-ADR-1426 kernel was fp32 throughout.
FLOAT_MATH = re.compile(r"\b(?:cbrtf|atan2f|sinf|cosf|expf|sqrtf|fabsf)\s*\(")
DEVICE_REDUCTION = re.compile(r"__shfl_\w+\s*\(|\batomicAdd\s*\(|__shared__")
# A math call whose first argument is not visibly a double.
UNPROMOTED = re.compile(r"\b(atan2|sin|cos|exp|sqrt)\(\s*(?!\(double\)|[0-9(]|ciede_sq|c_bar_7|h\b|2\.0)")
TERM_STORE = "reinterpret_cast<float *>(terms.data)[(size_t)y * width + x] ="
HOST_SUM = (
    "double de00_sum = 0.0;",
    "for (size_t i = 0u; i < count; i++)",
    "de00_sum += (double)terms[i];",
)
HOST_CALL = "ciede_frame_sum((const float *)s->rb.host_pinned, (size_t)s->frame_w * s->frame_h);"
HOST_SCORE = "const double score = 45. - 20. * log10(de00_sum / (s->frame_w * s->frame_h));"
DEVICE_POWF = "#define CIEDE_POWF(x, y) ((float)pow((double)(x), (double)(y)))"
HOST_POWF = "#define CIEDE_POWF(x, y) powf((x), (y))"
FP64_PIECES = (
    "return (float)pow(c, 1.0 / 3.0);",
    "return pow((c + A) * D, 2.4);",
    "float hue_angle = (float)atan2((double)x, (double)y);",
    "const float c1 = (float)sqrt(ciede_sq(color_1.a) + ciede_sq(color_1.b));",
    "const double c_bar_7 = pow((double)c_bar, 7.0);",
    "const float sixty = (float)(60.0 * exp((double)exponent));",
    "(double)r_sub_t * (double)chroma * (double)hue);",
)


def _code(source: str) -> str:
    """The source with its comments blanked, so prose cannot trip a check."""
    return COMMENT.sub(" ", source)


def _sources() -> dict[str, str]:
    return {
        name: (CUDA_ROOT / name).read_text(encoding="utf-8") for name in (HOST, KERNEL, DEVICE)
    }


def _arithmetic_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    device = _code(sources[DEVICE])
    for name in (KERNEL, DEVICE):
        if FLOAT_MATH.search(_code(sources[name])):
            failures.append(f"{name}: a float math function replaces the reference's fp64 call")
    for piece in FP64_PIECES:
        if piece not in device:
            failures.append(f"{DEVICE}: not the reference's expression ({piece})")
    unpromoted = UNPROMOTED.search(device)
    if unpromoted:
        failures.append(
            f"{DEVICE}: {unpromoted.group(1)}() takes an argument that is not written as a "
            "double, so the kernel's C++ picks the float overload"
        )
    if DEVICE_POWF not in sources[DEVICE] or HOST_POWF not in sources[DEVICE]:
        failures.append(f"{DEVICE}: CIEDE_POWF is not glibc's powf on the host and its CR value")
    if device.count("CIEDE_POWF(") != 4:
        failures.append(f"{DEVICE}: powf is used outside get_r_sub_t()'s two calls")
    return failures


def _sum_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    kernel = _code(sources[KERNEL])
    if kernel.count(TERM_STORE) != 2:
        failures.append(f"{KERNEL}: a kernel no longer stores each value at its raster position")
    if DEVICE_REDUCTION.search(kernel):
        failures.append(f"{KERNEL}: the per-pixel values are reduced on the device")
    if kernel.count("ciede_pixel(") != 2:
        failures.append(f"{KERNEL}: a kernel no longer calls the shared ciede_pixel()")
    device = _code(sources[DEVICE])
    for piece in HOST_SUM:
        if piece not in device:
            failures.append(f"{DEVICE}: ciede_frame_sum() is not one double in raster order")
    host = _code(sources[HOST])
    if HOST_CALL not in host:
        failures.append(f"{HOST}: collect no longer adds the whole plane")
    if HOST_SCORE not in host:
        failures.append(f"{HOST}: the score is not the reference's expression")
    return failures


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return _arithmetic_failures(sources) + _sum_failures(sources)


def _planted(name: str, old: str, new: str) -> list[str]:
    sources = _sources()
    if old not in sources[name]:
        raise AssertionError(f"{name}: planted regression anchor not found: {old}")
    sources[name] = sources[name].replace(old, new, 1)
    return _contract_failures(sources)


class CiedeCudaContract(unittest.TestCase):
    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_float_cube_root_is_detected(self) -> None:
        # The pre-ADR-1426 xyz_to_lab_map().
        failures = _planted(DEVICE, "return (float)pow(c, 1.0 / 3.0);", "return cbrtf((float)c);")
        self.assertTrue(any("float math function" in item for item in failures))

    def test_float_atan2_is_detected(self) -> None:
        # The pre-ADR-1426 get_h_prime_dev().
        failures = _planted(
            DEVICE,
            "float hue_angle = (float)atan2((double)x, (double)y);",
            "float hue_angle = atan2f(x, y);",
        )
        self.assertTrue(any("float math function" in item for item in failures))

    def test_unpromoted_argument_is_detected(self) -> None:
        # Valid C, but the kernel's C++ would call sin(float).
        failures = _planted(
            DEVICE, "sin((double)delta_h_prime / 2.0)", "sin(delta_h_prime / 2.0f)"
        )
        self.assertTrue(any("float overload" in item for item in failures))

    def test_device_powf_is_detected(self) -> None:
        failures = _planted(DEVICE, DEVICE_POWF, "#define CIEDE_POWF(x, y) powf((x), (y))")
        self.assertTrue(any("CIEDE_POWF" in item for item in failures))

    def test_warp_reduction_is_detected(self) -> None:
        # The pre-ADR-1426 warp_reduce_f32().
        sources = _sources()
        sources[
            KERNEL
        ] += "\nfloat r(float v) { return v + __shfl_down_sync(0xffffffff, v, 16); }\n"
        self.assertTrue(
            any("reduced on the device" in item for item in _contract_failures(sources))
        )

    def test_host_sum_of_block_partials_is_detected(self) -> None:
        # The pre-ADR-1426 collect_fex_cuda().
        failures = _planted(
            HOST,
            HOST_CALL,
            "ciede_frame_sum((const float *)s->rb.host_pinned, (size_t)s->partials_count);",
        )
        self.assertTrue(any("whole plane" in item for item in failures))

    def test_reordered_frame_sum_is_detected(self) -> None:
        failures = _planted(
            DEVICE, "for (size_t i = 0u; i < count; i++)", "for (size_t i = count; i-- > 0u;)"
        )
        self.assertTrue(any("raster order" in item for item in failures))


if __name__ == "__main__":
    unittest.main()
