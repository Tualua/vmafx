#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the SpEED SYCL covariance design (T-SPEED-CHROMA-SYCL-COV-1ULP-2026-10-06).

``speed.c::compute_cov_kernel_scalar()`` adds ``(x - mean_x) * (y - mean_y)``
into one fp64 running sum in raster order, rounding every add, and
``compute_covariance_row()`` stores ``(float)(sum / (width * height))``. The
SYCL twins have no fp64 type (ADR-0220), so ``speed_sycl_pipeline.cpp``:

- calls ``covariance_entry()`` of ``sycl_speed_cov_math.h``, which performs the
  reference's operations in the reference's order on values held in 64-bit
  integers (``sycl_soft_signed.h``): two differences, the product, the add,
  then the quotient and the conversion to fp32;
- runs one work-item per (channel, entry), so the sum is sequential;
- carries no parallel pair sum rounded once (``accumulate`` / ``ff_add`` /
  ``covariance_partial``): that design stored a neighbouring fp32 value on a
  cancelling entry (frame 140 of a 3840x1600 10-bit segment).

Device-free: reads the sources only. ``test_sycl_speed_cov_math`` checks the
entry against ``compute_cov_kernel_scalar()`` on the host and on a device.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE_ROOT = ROOT / "core" / "src" / "feature"

PIPELINE = FEATURE_ROOT / "sycl" / "speed_sycl_pipeline.cpp"
MATH = FEATURE_ROOT / "sycl" / "sycl_speed_cov_math.h"
REFERENCE = FEATURE_ROOT / "speed.c"
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def code(path: Path) -> str:
    return COMMENT.sub("", path.read_text(encoding="utf-8"))


def squash(text: str) -> str:
    return re.sub(r"\s+", " ", text)


class ReferenceLines(unittest.TestCase):
    """The lines the twin copies are still the reference's."""

    def test_reference_sum_is_sequential_fp64(self) -> None:
        text = squash(code(REFERENCE))
        for line in (
            "double val_x = data_x[i * stride_px + j];",
            "double val_y = data_y[i * stride_px + j];",
            "const double product = (val_x - mean_x) * (val_y - mean_y);",
            "result += product;",
            "float covariance = sums[k] / (dim->submatrix_width * dim->submatrix_height);",
        ):
            self.assertIn(line, text, f"speed.c no longer has: {line}")


class TwinDesign(unittest.TestCase):
    def test_math_header_replays_the_operations_in_order(self) -> None:
        text = squash(code(MATH))
        for line in (
            "for (uint32_t i = 0; i < height; i++)",
            "for (uint32_t j = 0; j < width; j++)",
            "const SoftSigned dx = covariance_difference(row_x[j], mx);",
            "const SoftSigned dy = covariance_difference(row_y[j], my);",
            "sum = signed_add(sum, covariance_term(dx, dy));",
            "return covariance_store(sum, static_cast<uint64_t>(width) * height);",
            # the shared helpers (ADR-1931): one implementation for A and the split chain
            "return vmaf_sycl_soft::signed_sub(vmaf_sycl_soft::signed_from_float(value), mean);",
            "return vmaf_sycl_soft::signed_mul(dx, dy);",
            "return vmaf_sycl_soft::signed_add(sum, vmaf_sycl_soft::signed_from_bits(term_bits));",
            "vmaf_sycl_soft::signed_div(sum, vmaf_sycl_soft::signed_from_exact(count));",
            "return vmaf_sycl_soft::signed_make(0u, 0, false);",
        ):
            self.assertIn(line, text, f"sycl_speed_cov_math.h lost: {line}")

    def test_math_header_has_no_fp64_type_and_no_pair_sum(self) -> None:
        text = code(MATH)
        self.assertIsNone(re.search(r"\bdouble\b", text), "fp64 type on the device")
        for name in ("two_sum", "two_prod", "ff_add", "quick_two_sum"):
            self.assertNotIn(name, text, f"{name}: a pair sum returned to the covariance")

    def test_pipeline_calls_the_entry_one_work_item_per_entry(self) -> None:
        text = squash(code(PIPELINE))
        self.assertIn("vmaf_sycl_speed_cov::covariance_entry(", text)
        self.assertIn("q.parallel_for(sycl::range<2>(channels, kTriangle)", text)
        self.assertIn('#include "sycl_speed_cov_math.h"', PIPELINE.read_text(encoding="utf-8"))

    def test_pipeline_has_no_parallel_pair_covariance(self) -> None:
        text = code(PIPELINE)
        for name in (
            "covariance_partial",
            "covariance_group",
            "covariance_group_size",
            "centred_product",
            "ff_div_to_float",
        ):
            self.assertNotIn(name, text, f"{name}: the pair-sum covariance came back")
        self.assertIsNone(
            re.search(r"\binline\s+Ff\s+accumulate\b", text), "the pair accumulate came back"
        )


if __name__ == "__main__":
    unittest.main()
