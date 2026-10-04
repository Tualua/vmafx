# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for :mod:`aiutils.onnx_signature`, the dependency-free ONNX reader
the registry validator uses to hold metadata to the shipped graphs."""

from __future__ import annotations

from pathlib import Path

import pytest

from aiutils.onnx_signature import OnnxWireError, TensorSignature, parse_signature, read_signature

_TINY = Path(__file__).resolve().parents[2] / "model" / "tiny"


def _varint(value: int) -> bytes:
    """Protobuf base-128 varint; a 64-bit value takes at most 10 bytes."""
    out = bytearray()
    for _ in range(10):
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            break
    return bytes(out)


def _field(number: int, payload: bytes) -> bytes:
    """A length-delimited protobuf field."""
    return _varint((number << 3) | 2) + _varint(len(payload)) + payload


def _value_info(name: str, *dims: int | str) -> bytes:
    dim_msgs = b"".join(
        _field(1, _varint(1 << 3) + _varint(d) if isinstance(d, int) else _field(2, d.encode()))
        for d in dims
    )
    tensor = _varint(1 << 3) + _varint(1) + _field(2, dim_msgs)  # elem_type FLOAT, shape
    return _field(1, name.encode()) + _field(2, _field(1, tensor))


def _model(graph: bytes, opsets: list[tuple[str, int]]) -> bytes:
    body = b"".join(
        _field(8, (_field(1, d.encode()) if d else b"") + _varint(2 << 3) + _varint(v))
        for d, v in opsets
    )
    return body + _field(7, graph)


def test_reads_shipped_feature_vector_model() -> None:
    sig = read_signature(_TINY / "vmaf_tiny_v2.onnx")
    assert sig.opsets == {"": 17}
    assert sig.default_opset == 17
    assert sig.inputs == [TensorSignature("features", ("N", 6))]
    assert sig.outputs == [TensorSignature("vmaf", ("N",))]


def test_reads_rank5_input_and_second_domain() -> None:
    sig = read_signature(_TINY / "transnet_v2.onnx")
    assert sig.opsets == {"": 17, "ai.onnx.ml": 2}
    assert sig.inputs == [TensorSignature("frames", (1, 100, 3, 27, 48))]
    assert sig.outputs == [TensorSignature("output_0", (1, 100))]


def test_reads_codec_input_of_external_data_model() -> None:
    sig = read_signature(_TINY / "fr_regressor_v2.onnx")
    assert [t.name for t in sig.inputs] == ["features", "codec"]
    assert sig.inputs[1].dims == ("batch", 14)


def test_initializer_listed_as_graph_input_is_dropped() -> None:
    """Before IR 4 every initializer was also a graph input."""
    graph = (
        _field(5, _field(8, b"w"))  # initializer named w
        + _field(11, _value_info("x", 1, 4))
        + _field(11, _value_info("w", 4, 4))
        + _field(12, _value_info("y", 1, 4))
    )
    sig = parse_signature(_model(graph, [("", 9)]))
    assert [t.name for t in sig.inputs] == ["x"]
    assert sig.outputs == [TensorSignature("y", (1, 4))]


def test_ai_onnx_domain_counts_as_default() -> None:
    sig = parse_signature(_model(_field(12, _value_info("y", 1)), [("ai.onnx", 18)]))
    assert sig.default_opset == 18


def test_no_default_domain_has_no_default_opset() -> None:
    sig = parse_signature(_model(_field(12, _value_info("y", 1)), [("ai.onnx.ml", 2)]))
    assert sig.default_opset is None


def test_truncated_field_is_an_error() -> None:
    data = _model(_field(12, _value_info("y", 1, 4)), [("", 17)])
    with pytest.raises(OnnxWireError):
        parse_signature(data[:-3])


def test_unknown_wire_type_is_an_error() -> None:
    with pytest.raises(OnnxWireError):
        parse_signature(bytes([(7 << 3) | 6, 0]))


def test_non_onnx_bytes_are_an_error() -> None:
    with pytest.raises(OnnxWireError):
        parse_signature(b"fixture-onnx")
