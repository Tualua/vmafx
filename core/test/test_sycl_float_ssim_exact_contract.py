#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the bit-exact float_ssim_sycl frame sums (ADR-1463).

``iqa/ssim_tools.c`` forms ``lv`` and ``cv`` per window in fp64 and ``sv`` in
fp32 (``ssim_accumulate_lane.h``) and adds ``lv * cv * sv``, lv, cv and sv
into one ``double`` each, window after window in raster order; the means are
returned as ``float``. A SYCL kernel has no fp64 type (ADR-0220), so
``float_ssim_sycl``:

- forms lv and cv as the CPU's doubles in 64-bit integers
  (``sycl_ssim_terms.h`` on ``sycl_soft_signed.h``), the CPU's operations one
  for one, and stores each window's term at its raster position, with no
  reduction on the device;
- adds the read-back planes on the host in index order, in ``double``.

Before ADR-1463 the kernel added fixed-point terms per work-group, which is
another sum of slightly different terms: on frames whose mean lies next to a
``float`` rounding boundary the result differed by one step.

Device-free: reads the sources only. Every planted regression below is a
construct of the old twin, another order, or an operation that rounds
elsewhere. ``test_sycl_float_ssim_parity`` checks the scores on the host and
on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE_ROOT = ROOT / "core" / "src" / "feature"

TWIN = "sycl/integer_ssim_sycl.cpp"
TERMS = "sycl/sycl_ssim_terms.h"
LANE = "iqa/ssim_accumulate_lane.h"
TOOLS = "iqa/ssim_tools.c"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
# The fixed-point twin shares the file; the float twin ends where it starts.
INTEGER_SECTION = "Real integer_ssim SYCL extractor (ADR-0564"

TERM_STORE = (
    "a_.terms[id[0] * (size_t)a_.final_width + id[1]] = "
    "ssim_product_bits(float_ssim_window_terms(a_, id[1], id[0]));"
)
LCS_STORES = (
    "a_.terms[index] = vmaf_sycl_soft::signed_bits(terms.luminance);",
    "a_.terms[windows + index] = vmaf_sycl_soft::signed_bits(terms.contrast);",
    "a_.structure[index] = terms.structure;",
)
SHAPES = (
    "constexpr int FSSIM_TERM_SG = 16;",
    "constexpr int FSSIM_TERM_GRF = 256;",
    "class FloatSsimTermKernel : public VmafSyclKernelShape<FSSIM_TERM_SG, FSSIM_TERM_GRF>",
    "class FloatSsimLcsKernel : public VmafSyclKernelShape<FSSIM_TERM_SG, FSSIM_TERM_GRF>",
)
FLATTENED = (
    "__attribute__((flatten, always_inline)) static inline SsimDoubleTerms "
    "float_ssim_window_terms("
)
HOST_CALLS = (
    "frame_sum_of_terms(s->h_terms, windows)",
    "ssim_frame_sums(s->h_terms, s->h_terms + windows, s->h_structure, windows);",
)
FRAME_SUM = (
    "for (size_t i = 0U; i < count; i++) {",
    "sum += std::bit_cast<double>(terms[i]);",
)
OLD_TWIN = ("reduce_over_group", "term_fixed(", "FixedSum", "d_partials")

# The CPU's operations, in its order, as the header writes them.
TERM_STEPS = {
    "2.0 * rm * cm is the exact product of the two converted means": (
        "signed_mul(signed_from_float(p.reference_mean), signed_from_float(p.comparison_mean));"
    ),
    "the luminance numerator is 2 * product + C1": (
        "l_num = signed_add(signed_twice(mean_product), signed_from_float(c1));"
    ),
    "the contrast numerator is 2 * srsc + C2": (
        "signed_add(signed_twice(signed_from_float(p.srsc)), signed_from_float(c2));"
    ),
    "lv is one quotient by the converted fp32 denominator": (
        ".luminance = signed_div(l_num, signed_from_float(p.l_den)),"
    ),
    "cv is one quotient by the converted fp32 denominator": (
        ".contrast = signed_div(c_num, signed_from_float(p.c_den)),"
    ),
    "the term is (lv * cv) * sv": (
        "signed_mul(signed_mul(t.luminance, t.contrast), signed_from_float(t.structure))"
    ),
}
HOST_STEPS = {
    "the host product is lv * cv * sv, left to right": "sums.ssim += lv * cv * sv;",
    "the luminance sum takes lv": "sums.luminance += lv;",
    "the contrast sum takes cv": "sums.contrast += cv;",
    "the structure sum takes sv": "sums.structure += sv;",
    "the sums take the windows in index order": "for (std::size_t i = 0U; i < count; i++) {",
}
ALWAYS_INLINED = (
    "VMAF_SYCL_ALWAYS_INLINE SsimDoubleTerms ssim_double_terms(",
    "VMAF_SYCL_ALWAYS_INLINE std::uint64_t ssim_product_bits(",
)

# The lines of the CPU reference the twin mirrors.
LANE_LINES = (
    "const double lv = (2.0 * rm * cm + C1) / l_den;",
    "const double cv = (2.0 * srsc + C2) / c_den;",
    "const double sv = sv_f;",
    "*local_ssim += lv * cv * sv;",
    "*local_l += lv;",
    "*local_c += cv;",
    "*local_s += sv;",
    "const float sv_f = (csb + C3) / (srsc + C3);",
)
TOOLS_LINES = (
    "return (float)(ssim_sum / (double)(w * h));",
    "*l_mean = (float)(l_sum / (double)(w * h));",
)


def _flat(source: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", source).split())


def _sources() -> dict[str, str]:
    return {
        name: (FEATURE_ROOT / name).read_text(encoding="utf-8")
        for name in (TWIN, TERMS, LANE, TOOLS)
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


def _twin_failures(twin: str) -> list[str]:
    whole = _flat(twin)
    code = _flat(twin[: twin.index(INTEGER_SECTION)])
    failures: list[str] = []
    if TERM_STORE not in code:
        failures.append(f"{TWIN}: the term is not stored at the window's raster position")
    if any(piece not in code for piece in LCS_STORES):
        failures.append(f"{TWIN}: the enable_lcs kernel does not store lv, cv and sv per window")
    if any(piece not in code for piece in SHAPES):
        failures.append(f"{TWIN}: a term kernel is not SIMD-16 with the 256-entry register file")
    if FLATTENED not in code:
        failures.append(f"{TWIN}: the window terms are not flattened into the kernel")
    for name in OLD_TWIN:
        if name in code:
            failures.append(f"{TWIN}: the float twin reduces on the device again ({name})")
    if any(piece not in code for piece in HOST_CALLS):
        failures.append(f"{TWIN}: collect no longer adds every window's terms")
    frame_sum = _function_body(whole, "frame_sum_of_terms")
    if any(piece not in frame_sum for piece in FRAME_SUM):
        failures.append(f"{TWIN}: the frame sum is not one double in raster order")
    return failures


def _header_failures(terms: str) -> list[str]:
    code = _flat(terms)
    failures = [
        f"{TERMS}: {what}: `{line}` is gone"
        for what, line in TERM_STEPS.items()
        if line not in code
    ]
    sums = _function_body(code, "accumulate_window") + _function_body(code, "ssim_frame_sums")
    failures += [
        f"{TERMS}: {what}: `{line}` is gone"
        for what, line in HOST_STEPS.items()
        if line not in sums
    ]
    if any(piece not in code for piece in ALWAYS_INLINED):
        failures.append(f"{TERMS}: a term function is not always inlined")
    return failures


def _reference_failures(lane: str, tools: str) -> list[str]:
    failures = [
        f"{LANE}: no longer holds `{line}`; the twin mirrors it"
        for line in LANE_LINES
        if line not in _flat(lane)
    ]
    failures += [
        f"{TOOLS}: no longer holds `{line}`; the twin mirrors it"
        for line in TOOLS_LINES
        if line not in _flat(tools)
    ]
    return failures


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return (
        _twin_failures(sources[TWIN])
        + _header_failures(sources[TERMS])
        + _reference_failures(sources[LANE], sources[TOOLS])
    )


def _replaced(name: str, old: str, new: str) -> list[str]:
    """The contract's failures with `old` replaced by `new` in one source."""
    sources = _sources()
    if old not in sources[name]:
        raise AssertionError(f"{name}: `{old}` not found, the planted regression is stale")
    sources[name] = sources[name].replace(old, new, 1)
    return _contract_failures(sources)


def _in_float_twin(text: str) -> list[str]:
    """The contract's failures with `text` inserted into the float twin."""
    sources = _sources()
    cut = sources[TWIN].index(INTEGER_SECTION)
    cut = sources[TWIN].rindex("/*", 0, cut)
    sources[TWIN] = sources[TWIN][:cut] + text + sources[TWIN][cut:]
    return _contract_failures(sources)


class FloatSsimSyclExactContract(unittest.TestCase):
    def _assert_detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_group_reduction_is_detected(self) -> None:
        # The pre-ADR-1463 store_fixed_group().
        failures = _in_float_twin(
            "static std::int64_t r(sycl::nd_item<2> item, std::int64_t value) {"
            " return sycl::reduce_over_group(item.get_group(), value,"
            " sycl::plus<std::int64_t>{}); }\n"
        )
        self._assert_detected(failures, "reduces on the device again")

    def test_fixed_point_term_is_detected(self) -> None:
        failures = _in_float_twin(
            "static std::int64_t f(vmaf_sycl_exact::Ff v) { return term_fixed(v); }\n"
        )
        self._assert_detected(failures, "reduces on the device again")

    def test_unstored_term_is_detected(self) -> None:
        failures = _replaced(
            TWIN,
            "a_.terms[id[0] * (size_t)a_.final_width + id[1]] =",
            "const std::uint64_t term =",
        )
        self._assert_detected(failures, "raster position")

    def test_lcs_kernel_without_contrast_is_detected(self) -> None:
        failures = _replaced(
            TWIN,
            "a_.terms[windows + index] = vmaf_sycl_soft::signed_bits(terms.contrast);",
            "",
        )
        self._assert_detected(failures, "lv, cv and sv per window")

    def test_wider_sub_group_is_detected(self) -> None:
        failures = _replaced(TWIN, SHAPES[0], "constexpr int FSSIM_TERM_SG = 32;")
        self._assert_detected(failures, "SIMD-16")

    def test_unflattened_terms_are_detected(self) -> None:
        failures = _replaced(
            TWIN,
            "__attribute__((flatten, always_inline)) static inline SsimDoubleTerms",
            "static inline SsimDoubleTerms",
        )
        self._assert_detected(failures, "not flattened")

    def test_host_sum_of_a_part_is_detected(self) -> None:
        failures = _replaced(
            TWIN,
            "frame_sum_of_terms(s->h_terms, windows)",
            "frame_sum_of_terms(s->h_terms, s->w_final)",
        )
        self._assert_detected(failures, "every window's terms")

    def test_reordered_frame_sum_is_detected(self) -> None:
        failures = _replaced(
            TWIN,
            "for (size_t i = 0U; i < count; i++) {\n        sum +=",
            "for (size_t i = count; i-- > 0U;) {\n        sum +=",
        )
        self._assert_detected(failures, "raster order")

    def test_reordered_lcs_sums_are_detected(self) -> None:
        failures = _replaced(
            TERMS,
            "    for (std::size_t i = 0U; i < count; i++) {\n        accumulate_window(",
            "    for (std::size_t i = count; i-- > 0U;) {\n        accumulate_window(",
        )
        self._assert_detected(failures, "index order")

    def test_regrouped_host_product_is_detected(self) -> None:
        # lv * (cv * sv) rounds elsewhere than (lv * cv) * sv.
        failures = _replaced(TERMS, "sums.ssim += lv * cv * sv;", "sums.ssim += lv * (cv * sv);")
        self._assert_detected(failures, "left to right")

    def test_regrouped_kernel_product_is_detected(self) -> None:
        failures = _replaced(
            TERMS,
            "signed_mul(signed_mul(t.luminance, t.contrast), signed_from_float(t.structure))",
            "signed_mul(t.luminance, signed_mul(t.contrast, signed_from_float(t.structure)))",
        )
        self._assert_detected(failures, "(lv * cv) * sv")

    def test_float_mean_product_is_detected(self) -> None:
        # (double)(rm * cm) rounds the product to fp32 first.
        failures = _replaced(
            TERMS,
            "signed_mul(signed_from_float(p.reference_mean), "
            "signed_from_float(p.comparison_mean));",
            "signed_from_float(p.reference_mean * p.comparison_mean);",
        )
        self._assert_detected(failures, "exact product")

    def test_reciprocal_quotient_is_detected(self) -> None:
        failures = _replaced(
            TERMS,
            ".contrast = signed_div(c_num, signed_from_float(p.c_den)),",
            ".contrast = signed_mul(c_num, signed_from_float(1.0f / p.c_den)),",
        )
        self._assert_detected(failures, "cv is one quotient")

    def test_plain_inline_is_detected(self) -> None:
        failures = _replaced(
            TERMS,
            "VMAF_SYCL_ALWAYS_INLINE std::uint64_t ssim_product_bits(",
            "inline std::uint64_t ssim_product_bits(",
        )
        self._assert_detected(failures, "not always inlined")

    def test_changed_reference_is_detected(self) -> None:
        failures = _replaced(LANE, "*local_ssim += lv * cv * sv;", "*local_ssim += lv * (cv * sv);")
        self._assert_detected(failures, "the twin mirrors it")


if __name__ == "__main__":
    unittest.main()
