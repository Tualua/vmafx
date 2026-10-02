#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the float ADM division (ADR-1442): a quotient, never a reciprocal estimate.

The decouple of float ADM computes ``k = t / o``. Upstream forms it as
``t * rcp_s(o)``, where ``rcp_s()`` refines the processor's RCPSS estimate
with one Newton step (``ADM_OPT_RECIP_DIVISION``). RCPSS is specified by an
error bound, not bit for bit, so the result is a property of the processor:
the same frame scored differently on different x86 hosts, differently again
on builds that do not use the instruction (MSVC, ARM), and a GPU twin could
only match the CPU of its own host by probing the estimate.

Since ADR-1442 the fork divides. The quotient is the correctly rounded fp32
one in the CPU reference, in ``float_adm_cuda`` and in ``float_adm_hip``
(ADR-1458), on every host and compiler. The two twins share their arithmetic
(``float_adm_gpu_common.h``): the CUDA kernels spell its division
``__fdiv_rn()``, the HIP kernels keep the plain ``/``, which hipcc rounds
correctly under ``-fhip-fp32-correctly-rounded-divide-sqrt``. This contract
fails when a reciprocal comes back by any of the
routes it once had or could take: the macro, an intrinsic in the scalar or
SIMD sources, the probed table of the twin, or a compiler flag that rewrites
a division.

Device-free: reads the sources only. ``test_float_adm_device_math`` checks
the value (inputs where estimate and quotient differ),
``test_cuda_float_adm_parity`` the CUDA device, and ``test_hip_float_adm_math``
every quotient of a million samples on a HIP device against the host.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE_SRC = ROOT / "core" / "src"
FEATURE_ROOT = CORE_SRC / "feature"

OPTIONS = "adm_options.h"
CPU = "adm_tools.c"
# The twins' shared arithmetic and its spelling per backend.
COMMON = "float_adm_gpu_common.h"
DEVICE = "cuda/float_adm/float_adm_device.h"
HIP_DEVICE = "hip/float_adm/float_adm_hip_math.h"
KERNEL = "cuda/float_adm/float_adm_score.cu"
HOST = "cuda/float_adm_cuda.c"
BUILD = "meson.build"
# The float ADM reference; every twin and SIMD path (any file under feature/
# with float_adm in its name) is scanned as well.
REFERENCE_SOURCES = (CPU, "adm.c")
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".cu", ".cuh", ".hip", ".metal", ".mm")
REMOVED_FILES = ("adm_reciprocal_model.c", "adm_reciprocal_model.h")

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
MACRO_DEFINED = re.compile(r"^[ \t]*#[ \t]*define[ \t]+ADM_OPT_RECIP_DIVISION\b", re.M)
# x86 and ARM reciprocal-estimate intrinsics, the helpers the reference had,
# and the twin's probe and table.
RECIPROCAL = re.compile(
    r"_mm\d*_(?:mask_|maskz_)?rcp(?:14|28)?_[ps][sd]\b|\bvrecp[es]q?_f32\b|\brcp_s\b"
    r"|\brcp_estimate_s\b|\badm_divs_\w+\b|\badm_reciprocal_model\w*\b|\brcp_table\b"
    r"|\bADM_DIVISION_\w+\b"
)
DIVS = "#define DIVS(n, d) ((n) / (d))"
MACRO_REFUSED = '#error "ADM_OPT_RECIP_DIVISION is not supported: float ADM divides (ADR-1442)"'
DECOUPLE_QUOTIENT = "float k = DIVS(t, o + eps);"
COMMON_DIVS = (
    "FADM_HD float fadm_divs(float n, float d) { return FADM_FDIV(n, d); }",
    "float k = fadm_divs(t, FADM_FADD(o, FADM_EPS));",
    "#define FADM_FDIV(a, b) ((float)((a) / (b)))",
)
CUDA_DIVS = "#define FADM_FDIV(a, b) __fdiv_rn((a), (b))"
# What makes the plain `/` of a HIP kernel the correctly rounded quotient.
HIP_DIVISION_FLAG = "-fhip-fp32-correctly-rounded-divide-sqrt"
HIP_STRICT_FP = re.compile(r"^\s*hip_strict_fp_args = \[(.*?)\]", re.M)
# Compiler options that let a division become a reciprocal multiply.
FAST_DIVISION = re.compile(
    r"use_fast_math|-prec-div=false|-ffast-math|-Ofast|-freciprocal-math|-mrecip"
    r"|-funsafe-math-optimizations"
)


def _code(source: str) -> str:
    """The source with its comments blanked, so prose cannot trip a check."""
    return COMMENT.sub(" ", source)


def _flat(source: str) -> str:
    """Code with every run of whitespace collapsed, so line breaks do not matter."""
    return " ".join(_code(source).split())


def _float_adm_sources() -> tuple[str, ...]:
    """The reference and every float_adm file of every backend and SIMD path."""
    found = {
        path.relative_to(FEATURE_ROOT).as_posix()
        for path in FEATURE_ROOT.rglob("*float_adm*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    }
    return tuple(sorted(found | set(REFERENCE_SOURCES)))


def _sources() -> dict[str, str]:
    sources = {
        name: (FEATURE_ROOT / name).read_text(encoding="utf-8")
        for name in (OPTIONS, *_float_adm_sources())
    }
    sources[BUILD] = (CORE_SRC / BUILD).read_text(encoding="utf-8")
    return sources


def _reference_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    if MACRO_DEFINED.search(_code(sources[OPTIONS])):
        failures.append(f"{OPTIONS}: ADM_OPT_RECIP_DIVISION is defined")
    cpu = _code(sources[CPU])
    if DIVS not in cpu or cpu.count("#define DIVS(") != 1:
        failures.append(f"{CPU}: DIVS() is not the one plain quotient")
    if MACRO_REFUSED not in cpu:
        failures.append(f"{CPU}: a build with ADM_OPT_RECIP_DIVISION is no longer refused")
    if DECOUPLE_QUOTIENT not in cpu:
        failures.append(f"{CPU}: the decouple's ratio is not DIVS(t, o + eps)")
    return failures


def _reciprocal_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    for name in sorted(set(sources) - {OPTIONS, BUILD}):
        found = RECIPROCAL.search(_code(sources[name]))
        if found:
            failures.append(f"{name}: a reciprocal estimate is back ({found.group(0)})")
    return failures


def _twin_failures(sources: dict[str, str]) -> list[str]:
    common = _flat(sources[COMMON])
    failures = [
        f"{COMMON}: the twins' division is not the correctly rounded quotient ({piece})"
        for piece in COMMON_DIVS
        if piece not in common
    ]
    if CUDA_DIVS not in _flat(sources[DEVICE]):
        failures.append(f"{DEVICE}: the CUDA division is not the correctly rounded quotient")
    # A HIP kernel divides with the shared header's plain `/`: no spelling of
    # its own, and the flag that rounds it correctly in the kernels' list.
    if re.search(r"\bFADM_FDIV\b|__fdiv_rn", _code(sources[HIP_DEVICE])):
        failures.append(f"{HIP_DEVICE}: the HIP division is not the shared header's plain `/`")
    strict = HIP_STRICT_FP.search(_code_meson(sources[BUILD]))
    if strict is None or HIP_DIVISION_FLAG not in strict.group(1):
        failures.append(f"{BUILD}: the HIP kernels' `/` is not the correctly rounded quotient")
    return failures


def _build_failures(sources: dict[str, str]) -> list[str]:
    found = FAST_DIVISION.search(_code_meson(sources[BUILD]))
    if found:
        return [f"{BUILD}: {found.group(0)} lets the compiler replace a division"]
    return []


def _code_meson(source: str) -> str:
    """A Meson file without its `#` comments."""
    return "\n".join(line.split("#", 1)[0] for line in source.splitlines())


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return (
        _reference_failures(sources)
        + _reciprocal_failures(sources)
        + _twin_failures(sources)
        + _build_failures(sources)
    )


def _planted(name: str, old: str, new: str) -> list[str]:
    sources = _sources()
    if old not in sources[name]:
        raise AssertionError(f"{name}: planted regression anchor not found: {old}")
    sources[name] = sources[name].replace(old, new, 1)
    return _contract_failures(sources)


class FloatAdmDividesContract(unittest.TestCase):
    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_every_backend_is_scanned(self) -> None:
        scanned = _float_adm_sources()
        for name in (
            CPU,
            COMMON,
            DEVICE,
            HIP_DEVICE,
            KERNEL,
            HOST,
            "x86/float_adm_avx512.c",
            "arm64/float_adm_neon.c",
        ):
            self.assertIn(name, scanned)
        for backend in ("sycl", "hip", "metal"):
            self.assertTrue(any(name.startswith(f"{backend}/") for name in scanned), backend)

    def test_the_model_files_are_gone(self) -> None:
        for name in REMOVED_FILES:
            self.assertFalse((FEATURE_ROOT / name).exists(), name)
        self.assertNotIn("adm_reciprocal_model", _sources()[BUILD])

    def test_upstream_macro_is_detected(self) -> None:
        # What an upstream sync of adm_options.h brings back.
        sources = _sources()
        sources[OPTIONS] += "\n#define ADM_OPT_RECIP_DIVISION\n"
        self.assertTrue(any("is defined" in item for item in _contract_failures(sources)))

    def test_upstream_reciprocal_is_detected(self) -> None:
        # What an upstream sync of adm_tools.c brings back.
        failures = _planted(
            CPU,
            DIVS,
            "static float rcp_s(float x) { float xi = _mm_cvtss_f32(_mm_rcp_ss(_mm_load_ss(&x)));"
            " return xi + xi * (1.0f - x * xi); }\n#define DIVS(n, d) ((n) * rcp_s(d))",
        )
        self.assertTrue(any("not the one plain quotient" in item for item in failures))
        self.assertTrue(any("a reciprocal estimate is back" in item for item in failures))

    def test_dropped_guard_is_detected(self) -> None:
        failures = _planted(CPU, MACRO_REFUSED, "")
        self.assertTrue(any("no longer refused" in item for item in failures))

    def test_simd_reciprocal_is_detected(self) -> None:
        sources = _sources()
        sources["x86/float_adm_avx512.c"] += "\n__m512 f(__m512 x) { return _mm512_rcp14_ps(x); }\n"
        sources[
            "arm64/float_adm_neon.c"
        ] += "\nfloat32x4_t g(float32x4_t x) { return vrecpeq_f32(x); }\n"
        failures = _contract_failures(sources)
        self.assertTrue(any("x86/float_adm_avx512.c" in item for item in failures))
        self.assertTrue(any("arm64/float_adm_neon.c" in item for item in failures))

    def test_probed_table_in_the_twin_is_detected(self) -> None:
        # The ADR-1420 twin.
        failures = _planted(
            COMMON,
            "return FADM_FDIV(n, d);",
            "return FADM_FMUL(n, FADM_FROM_BITS(adm_reciprocal_model_bits(rcp_table, FADM_BITS(d))));",
        )
        self.assertTrue(any("correctly rounded quotient" in item for item in failures))
        self.assertTrue(any("a reciprocal estimate is back" in item for item in failures))

    def test_plain_device_division_is_detected(self) -> None:
        # `/` on the device is correctly rounded only while no flag says otherwise.
        failures = _planted(
            DEVICE,
            "#define FADM_FDIV(a, b) __fdiv_rn((a), (b))",
            "#define FADM_FDIV(a, b) ((a) / (b))",
        )
        self.assertTrue(any("correctly rounded quotient" in item for item in failures))

    def test_reciprocal_multiply_in_the_hip_spelling_is_detected(self) -> None:
        sources = _sources()
        sources[HIP_DEVICE] += "\n#define FADM_FDIV(a, b) ((a) * (1.0f / (b)))\n"
        self.assertTrue(
            any("shared header's plain" in item for item in _contract_failures(sources))
        )

    def test_hip_division_flag_dropped_is_detected(self) -> None:
        # Without the flag hipcc's fp32 `/` is an approximate reciprocal multiply.
        failures = _planted(
            BUILD,
            "hip_strict_fp_args = ['-ffp-contract=off', '-fhip-fp32-correctly-rounded-divide-sqrt']",
            "hip_strict_fp_args = ['-ffp-contract=off']",
        )
        self.assertTrue(any("HIP kernels' `/`" in item for item in failures))

    def test_fast_math_flag_is_detected(self) -> None:
        sources = _sources()
        sources[BUILD] += "\ncuda_flags += ['--use_fast_math']\n"
        self.assertTrue(any("replace a division" in item for item in _contract_failures(sources)))


if __name__ == "__main__":
    unittest.main()
