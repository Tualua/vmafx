#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Quantize a trained ONNX model to INT8 using static PTQ.

Produces a quantized ``model/*_int8.onnx`` alongside the float32 original.
The quantization would take a calibration dataset as ``ptq_static.py`` does (a
hand-made ``.npz``); ``vmaf-train quantize-int8`` already quantizes the
feature-vector regressors from a parquet feature cache.

See docs/research/0090-phase-a-promotion-audit-2026-05-08.md for the
quantization strategy and accuracy-drop targets.

NOT YET IMPLEMENTED — exits with a clear message and a non-zero status.
"""

from __future__ import annotations

import sys


def main() -> int:
    """Report that the script is not implemented; the exit status is 1."""
    print(
        "quantize_int8.py: not yet implemented.\n"
        "See docs/research/0090-phase-a-promotion-audit-2026-05-08.md\n"
        "for the quantization strategy and accuracy-drop targets.\n"
        "Use `vmaf-train quantize-int8` for the feature-vector regressors or\n"
        "ptq_static.py with a calibration .npz.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
