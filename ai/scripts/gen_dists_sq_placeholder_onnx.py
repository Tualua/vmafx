#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Generate the DISTS-Sq smoke placeholder ONNX (``model/tiny/dists_sq.onnx``).

The graph is a plain mean squared distance between two ImageNet-normalised
RGB tensors (``Sub`` -> ``Mul`` -> ``ReduceMean``); it exercises the
``dists_sq`` extractor's load and session path and is not a DISTS model.

I/O contract (names are stable; real weights must honour them):

  inputs:  ``ref``, ``dist`` -- float32 ``[1, 3, H, W]``
  output:  ``score``         -- float32 scalar

See docs/ai/models/dists_sq.md and docs/research/0111-dists-sq-extractor-2026-05-14.md.

The serialisation is deterministic: ``--check`` compares the committed file
byte for byte with a fresh build and exits 1 on any difference. The sidecar
(``dists_sq.json``) and the registry entry are committed JSON, not written
here.

Usage::

    python3 ai/scripts/gen_dists_sq_placeholder_onnx.py [--output PATH] [--check]
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from onnx import TensorProto, helper

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = REPO_ROOT / "model" / "tiny" / "dists_sq.onnx"
OPSET = 17
IR_VERSION = 8


def build_model_bytes() -> bytes:
    """Return the serialised placeholder graph."""
    nodes = [
        helper.make_node("Sub", ["ref", "dist"], ["delta"], name="delta"),
        helper.make_node("Mul", ["delta", "delta"], ["delta_sq"], name="square"),
        helper.make_node("ReduceMean", ["delta_sq"], ["score"], name="mean_distance", keepdims=0),
    ]
    inputs = [
        helper.make_tensor_value_info(name, TensorProto.FLOAT, [1, 3, "H", "W"])
        for name in ("ref", "dist")
    ]
    output = helper.make_tensor_value_info("score", TensorProto.FLOAT, [])
    graph = helper.make_graph(nodes, "vmaf_tiny_dists_sq_placeholder_v0", inputs, [output])
    model = helper.make_model(
        graph,
        producer_name="vmaf-tiny-dists-sq-placeholder",
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
            print(f"gen_dists_sq_placeholder_onnx: {args.output} is missing", file=sys.stderr)
            return 1
        if args.output.read_bytes() != data:
            print(
                f"gen_dists_sq_placeholder_onnx: {args.output} differs from a fresh build "
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
