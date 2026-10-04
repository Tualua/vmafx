#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Generate the MobileSal smoke placeholder ONNX (``model/tiny/mobilesal.onnx``).

A single 1x1 ``Conv`` (the mean of the three channels) followed by
``Sigmoid``: a 3-channel-input, 1-channel-output graph that exercises the
load / session plumbing of the ``mobilesal`` extractor (ADR-0218) without the
MobileSal training pipeline.

I/O contract (tensor names are stable -- a real MobileSal drop-in must honour
these names without C changes):

  input:  ``input``        -- float32 ``[1, 3, H, W]``, pixels in [0, 1]
  output: ``saliency_map`` -- float32 ``[1, 1, H, W]``, in [0, 1]

See docs/adr/0218-mobilesal-saliency-extractor.md.

The serialisation is deterministic: ``--check`` compares the committed file
byte for byte with a fresh build and exits 1 on any difference. The sidecar
(``mobilesal.json``) and the registry entry are committed JSON, not written
here.

Usage::

    python3 ai/scripts/gen_mobilesal_placeholder_onnx.py [--output PATH] [--check]
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

import numpy as np
from onnx import TensorProto, helper, numpy_helper

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = REPO_ROOT / "model" / "tiny" / "mobilesal.onnx"
OPSET = 17
IR_VERSION = 8


def build_model_bytes() -> bytes:
    """Return the serialised placeholder graph."""
    weight = numpy_helper.from_array(np.full((1, 3, 1, 1), 1.0 / 3.0, dtype=np.float32), "conv_w")
    bias = numpy_helper.from_array(np.zeros((1,), dtype=np.float32), "conv_b")
    nodes = [
        helper.make_node(
            "Conv", ["input", "conv_w", "conv_b"], ["conv_out"], name="conv0", kernel_shape=[1, 1]
        ),
        helper.make_node("Sigmoid", ["conv_out"], ["saliency_map"], name="sigmoid0"),
    ]
    graph = helper.make_graph(
        nodes,
        "vmaf_tiny_mobilesal_placeholder_v0",
        [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, "H", "W"])],
        [helper.make_tensor_value_info("saliency_map", TensorProto.FLOAT, [1, 1, "H", "W"])],
        initializer=[weight, bias],
    )
    model = helper.make_model(
        graph,
        producer_name="vmaf-tiny-mobilesal-placeholder",
        producer_version="0",
        opset_imports=[helper.make_opsetid("", OPSET)],
    )
    model.ir_version = IR_VERSION
    data: bytes = model.SerializeToString()
    return data


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="compare --output with a fresh build instead of writing it",
    )
    args = parser.parse_args(argv)

    data = build_model_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if args.check:
        if not args.output.is_file():
            print(f"gen_mobilesal_placeholder_onnx: {args.output} is missing", file=sys.stderr)
            return 1
        if args.output.read_bytes() != data:
            print(
                f"gen_mobilesal_placeholder_onnx: {args.output} differs from a fresh build "
                f"(sha256 {digest})",
                file=sys.stderr,
            )
            return 1
        print(f"{args.output}: identical to a fresh build (sha256 {digest})")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes, sha256 {digest})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
