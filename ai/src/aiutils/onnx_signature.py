# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Read an ONNX model's opsets and graph inputs/outputs without ``onnx``.

``validate_model_registry.py`` runs in a CI job that installs only
``jsonschema``; it still has to hold the registry and the sidecars to the
graphs they describe. This module walks the protobuf wire format of a
``ModelProto`` far enough to read ``opset_import`` and the ``input`` /
``output`` value infos of the main graph, skipping every other field
(nodes, initializers, external-data references) by its length prefix, so a
30 MB model costs one pass over its top-level fields.

Field numbers come from ``onnx/onnx.proto``: ModelProto ``graph`` = 7,
``opset_import`` = 8; OperatorSetIdProto ``domain`` = 1, ``version`` = 2;
GraphProto ``input`` = 11, ``output`` = 12, ``initializer`` = 5;
ValueInfoProto ``name`` = 1, ``type`` = 2; TypeProto ``tensor_type`` = 1;
Tensor ``shape`` = 2; TensorShapeProto ``dim`` = 1; Dimension
``dim_value`` = 1, ``dim_param`` = 2.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

_WIRE_VARINT = 0
_WIRE_I64 = 1
_WIRE_LEN = 2
_WIRE_I32 = 5


class OnnxWireError(ValueError):
    """The bytes are not a well-formed protobuf message."""


@dataclass(frozen=True)
class TensorSignature:
    """One graph input or output: its name and dims (int, or the dim_param)."""

    name: str
    dims: tuple[int | str, ...]


@dataclass
class OnnxSignature:
    """Opsets by domain ('' is the default ONNX domain) and the graph I/O."""

    opsets: dict[str, int] = field(default_factory=dict)
    inputs: list[TensorSignature] = field(default_factory=list)
    outputs: list[TensorSignature] = field(default_factory=list)

    @property
    def default_opset(self) -> int | None:
        """Opset of the default domain ('' or 'ai.onnx'), None when absent."""
        if "" in self.opsets:
            return self.opsets[""]
        return self.opsets.get("ai.onnx")


def _varint(buf: memoryview, pos: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 70, 7):
        if pos >= len(buf):
            raise OnnxWireError("truncated varint")
        byte = buf[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, pos
    raise OnnxWireError("varint longer than 10 bytes")


_Value = int | memoryview | None


def _fields(buf: memoryview) -> Iterator[tuple[int, int, _Value]]:
    """Yield (field number, wire type, value) for every field of a message.

    ``value`` is an int for varints and a memoryview for length-delimited
    fields; fixed-width fields are skipped and yielded as None.
    """
    pos = 0
    end = len(buf)
    while pos < end:
        key, pos = _varint(buf, pos)
        number, wire = key >> 3, key & 7
        if wire == _WIRE_VARINT:
            value, pos = _varint(buf, pos)
            yield number, wire, value
        elif wire == _WIRE_LEN:
            length, pos = _varint(buf, pos)
            if pos + length > end:
                raise OnnxWireError("length-delimited field runs past the message")
            yield number, wire, buf[pos : pos + length]
            pos += length
        elif wire in (_WIRE_I64, _WIRE_I32):
            pos += 8 if wire == _WIRE_I64 else 4
            yield number, wire, None
        else:
            raise OnnxWireError(f"unsupported wire type {wire}")


def _opset(buf: memoryview) -> tuple[str, int]:
    domain = ""
    version = 0
    for number, _wire, value in _fields(buf):
        if number == 1 and isinstance(value, memoryview):
            domain = bytes(value).decode("utf-8")
        elif number == 2 and isinstance(value, int):
            version = value
    return domain, version


def _dims(shape: memoryview) -> tuple[int | str, ...]:
    dims: list[int | str] = []
    for number, _wire, dim in _fields(shape):
        if number != 1 or not isinstance(dim, memoryview):
            continue
        entry: int | str = "?"
        for dnum, _dwire, dval in _fields(dim):
            if dnum == 1 and isinstance(dval, int):
                entry = dval
            elif dnum == 2 and isinstance(dval, memoryview):
                entry = bytes(dval).decode("utf-8")
        dims.append(entry)
    return tuple(dims)


def _tensor_dims(type_proto: memoryview) -> tuple[int | str, ...]:
    for number, _wire, tensor in _fields(type_proto):
        if number != 1 or not isinstance(tensor, memoryview):
            continue
        for tnum, _twire, shape in _fields(tensor):
            if tnum == 2 and isinstance(shape, memoryview):
                return _dims(shape)
    return ()


def _value_info(buf: memoryview) -> TensorSignature:
    name = ""
    dims: tuple[int | str, ...] = ()
    for number, _wire, value in _fields(buf):
        if number == 1 and isinstance(value, memoryview):
            name = bytes(value).decode("utf-8")
        elif number == 2 and isinstance(value, memoryview):
            dims = _tensor_dims(value)
    return TensorSignature(name=name, dims=dims)


def _graph_io(graph: memoryview, sig: OnnxSignature) -> None:
    initializers: set[str] = set()
    inputs: list[TensorSignature] = []
    for number, _wire, value in _fields(graph):
        if not isinstance(value, memoryview):
            continue
        if number == 11:
            inputs.append(_value_info(value))
        elif number == 12:
            sig.outputs.append(_value_info(value))
        elif number == 5:
            initializers.add(_initializer_name(value))
    # Before IR 4 an initializer was listed as a graph input as well.
    sig.inputs.extend(t for t in inputs if t.name not in initializers)


def _initializer_name(tensor: memoryview) -> str:
    for number, _wire, value in _fields(tensor):
        if number == 8 and isinstance(value, memoryview):  # TensorProto.name
            return bytes(value).decode("utf-8")
    return ""


def parse_signature(data: bytes) -> OnnxSignature:
    """Parse the opsets and the main graph's I/O from serialized model bytes."""
    sig = OnnxSignature()
    buf = memoryview(data)
    for number, _wire, value in _fields(buf):
        if not isinstance(value, memoryview):
            continue
        if number == 8:
            domain, version = _opset(value)
            sig.opsets[domain] = version
        elif number == 7:
            _graph_io(value, sig)
    return sig


def read_signature(path: Path) -> OnnxSignature:
    """Read the opsets and graph I/O of the ONNX file at @p path."""
    return parse_signature(Path(path).read_bytes())
