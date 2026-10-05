#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""No GPU source advances a pointer to samples wider than a byte by a byte stride.

A `VmafPicture` row stride, a device pitch from `cuMemAllocPitch` or
`hipMallocPitch`, and the strides the twins pass to their kernels count bytes.
Above 8 bits a sample is two bytes, so a `uint16_t *` advanced by `y * stride`
lands on row `2 * y` and reads past the plane for the lower half of the
picture. An upstream CUDA motion kernel had exactly this defect for 16-bit
input; the fork's kernels address a row through a byte pointer and cast the
row (`reinterpret_cast<const T *>(plane + y * stride)`), or convert the stride
to elements first (`stride / sizeof(T)`).

Device-free. The scan covers every C, C++, CUDA, HIP, SYCL and Metal source and
header under `core/src` in a `cuda`, `hip`, `sycl` or `metal` directory, kernels
and host dispatch, with comments and string literals removed. It reports:

- a cast to a pointer whose element is not a byte type (`reinterpret_cast`,
  `static_cast` or C style), whose result is then offset (`+`) or indexed
  (`[]`) by an expression that names a stride or pitch;
- a pointer variable declared with such an element type, offset or indexed by
  a picture's byte stride (`->stride[` or `.stride[`).

An expression that converts the stride (`/ sizeof`, `/ 2`, `>> 1`) or names an
element count (`_elems`, `_elements`, `_px`, `_samples`, `_words`) is
accepted. The planted cases hold the upstream form, its variants and the
correct forms, and the live motion SAD kernel edited back to the upstream form
must fail.
"""

from __future__ import annotations

import re
import unittest
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "core" / "src"
BACKEND_DIRS = frozenset({"cuda", "hip", "sycl", "metal"})
SUFFIXES = frozenset({".c", ".h", ".cpp", ".hpp", ".cu", ".cuh", ".hip", ".metal", ".mm"})
BYTE_TYPES = frozenset({"uint8_t", "int8_t", "char", "uchar", "std::byte", "void", "auto"})
MOTION_SAD = SRC / "feature" / "cuda" / "integer_motion_v2" / "motion_v2_score.cu"
MOTION_SAD_ROW = "reinterpret_cast<const T *>(plane + ((ptrdiff_t)y * stride)) + x"
MOTION_SAD_UPSTREAM = "reinterpret_cast<const T *>(plane) + ((ptrdiff_t)y * stride) + x"
MIN_SCANNED = 200
# Qualifiers and address spaces that may precede the element type of a pointer.
QUALIFIER = (
    r"(?:(?:const|volatile|unsigned|signed|device|constant|thread|threadgroup|__global)\s+)*"
)
POINTER = QUALIFIER + r"([A-Za-z_][\w:]*)(?:\s+(?:const|__restrict__|restrict))*\s*\*"
CAST = re.compile(
    r"\b(?:reinterpret|static)_cast\s*<\s*" + POINTER + r"\s*>\s*\(|\(\s*" + POINTER + r"\s*\)"
)
DECLARATION = re.compile(
    QUALIFIER + r"\b([A-Za-z_][\w:]*)\s*\*\s*(?:(?:const|__restrict__|restrict)\s+)*"
    r"([A-Za-z_]\w*)\s*[=;,)]"
)
NOT_A_TYPE = frozenset({"return", "const", "struct", "else", "sizeof", "case", "delete"})
STRIDE = re.compile(r"\b\w*(?:stride|pitch)\w*\b", re.I)
PICTURE_STRIDE = re.compile(r"(?:->|\.)stride\s*\[")
CONVERTED = re.compile(
    r"/\s*sizeof|/\s*2\b|>>\s*1\b|_elems?\b|_elements\b|_px\b|_samples?\b|_words?\b", re.I
)
OPERAND = re.compile(r"\s*[\w.>\-\[\]]+")
SCAN_LIMIT = 4000
TAIL_LIMIT = 600


@dataclass(frozen=True)
class Finding:
    """One pointer advanced by a byte stride."""

    path: str
    line: int
    element: str
    text: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.element} pointer: {self.text}"


def code(source: str) -> str:
    """`source` without comments and string literals; line numbers are kept."""
    source = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', source)


def closing(text: str, start: int, pair: str) -> int:
    """Index of the bracket closing the one at `start`, or -1."""
    depth = 0
    for index in range(start, min(len(text), start + SCAN_LIMIT)):
        if text[index] == pair[0]:
            depth += 1
        elif text[index] == pair[1]:
            depth -= 1
            if depth == 0:
                return index
    return -1


def skip_space(text: str, index: int, step: int) -> int:
    """First index from `index` in direction `step` that is not white space."""
    while 0 <= index < len(text) and text[index] in " \t\n":
        index += step
    return index


def offset_after(text: str, index: int) -> str:
    """The `[...]` or `+ ...` applied to the value that ends before `index`."""
    index = skip_space(text, index, 1)
    if index < len(text) and text[index] == "[":
        end = closing(text, index, "[]")
        return text[index : end + 1] if end > 0 else ""
    if index >= len(text) or text[index] != "+":
        return ""
    depth, out = 0, []
    for char in text[index : index + TAIL_LIMIT]:
        if depth == 0 and char in ")]};,?:":
            break
        depth += (char in "([{") - (char in ")]}")
        out.append(char)
    return "".join(out)


def cast_end(text: str, match: re.Match[str]) -> int:
    """Index of the last character of the cast's operand, or -1."""
    if match.group(0).endswith("("):
        return closing(text, match.end() - 1, "()")
    after = skip_space(text, match.end(), 1)
    if after < len(text) and text[after] == "(":
        return closing(text, after, "()")
    operand = OPERAND.match(text, match.end())
    return operand.end() - 1 if operand else -1


def unwrap(text: str, start: int, end: int) -> int:
    """`end` moved past the parentheses that only wrap the cast expression."""
    before = skip_space(text, start - 1, -1)
    for _ in range(SCAN_LIMIT):
        after = skip_space(text, end + 1, 1)
        if before < 0 or text[before] != "(" or after >= len(text) or text[after] != ")":
            return end
        end, before = after, skip_space(text, before - 1, -1)
    return end


def byte_stride(tail: str, stride: re.Pattern[str]) -> bool:
    """True when `tail` names a byte stride it does not convert to elements."""
    return bool(tail) and bool(stride.search(tail)) and not CONVERTED.search(tail)


def cast_findings(path: str, text: str) -> list[Finding]:
    """Casts to a wide element pointer offset or indexed by a stride."""
    found = []
    for match in CAST.finditer(text):
        element = match.group(1) or match.group(2)
        end = cast_end(text, match)
        if element in BYTE_TYPES or end < 0:
            continue
        end = unwrap(text, match.start(), end)
        tail = offset_after(text, end + 1)
        if byte_stride(tail, STRIDE):
            line = text.count("\n", 0, match.start()) + 1
            snippet = " ".join(text[match.start() : end + 1 + len(tail)].split())
            found.append(Finding(path, line, element, snippet[:160]))
    return found


def variable_findings(path: str, text: str) -> list[Finding]:
    """Wide element pointer variables offset or indexed by a picture's byte stride."""
    wide = {
        match.group(2): match.group(1)
        for match in DECLARATION.finditer(text)
        if match.group(1) not in BYTE_TYPES | NOT_A_TYPE
    }
    found = []
    for name, element in sorted(wide.items()):
        for use in re.finditer(r"\b" + re.escape(name) + r"\s*(?=[\[+])", text):
            tail = offset_after(text, use.end())
            if byte_stride(tail, PICTURE_STRIDE):
                line = text.count("\n", 0, use.start()) + 1
                found.append(Finding(path, line, element, " ".join((name + tail).split())[:160]))
    return found


def scan_text(path: str, source: str) -> list[Finding]:
    """Every finding in one source file."""
    text = code(source)
    return cast_findings(path, text) + variable_findings(path, text)


def gpu_sources(root: Path = SRC) -> list[Path]:
    """The scanned files: every source under a backend directory of `root`."""
    return sorted(
        path
        for path in root.rglob("*")
        if path.suffix in SUFFIXES and BACKEND_DIRS & set(path.relative_to(root).parts)
    )


def scan_tree(root: Path = SRC) -> list[Finding]:
    """Every finding under `root`."""
    found: list[Finding] = []
    for path in gpu_sources(root):
        found += scan_text(str(path.relative_to(root)), path.read_text(encoding="utf-8"))
    return found


# Defective forms: each must give exactly one finding.
PLANTED = {
    "upstream motion row": (
        "blurred_y += filter_d[yf] * (reinterpret_cast<const uint16_t*>(src.data[0])\n"
        "    + mirror(y-radius+yf, height) * src.stride[0])[mirror(x-radius+xf, width)];"
    ),
    "template element": "return (int)__ldg(" + MOTION_SAD_UPSTREAM + ");",
    "static_cast": "const float *row = static_cast<const float *>(base) + y * pitch;",
    "c-style cast": "const uint16_t *row = (const uint16_t *)pic->data[0] + y * pic->stride[0];",
    "wrapped and indexed": "v = ((const uint16_t *)(base))[y * src_stride + x];",
    "metal address space": "v = ((device const ushort *)(src))[y * stride + x];",
    "typed variable": (
        "const uint16_t *ref = (const uint16_t *)ref_pic->data[0];\n"
        "x = ref[i * ref_pic->stride[0] + j];"
    ),
}
# Correct forms: none may give a finding.
CORRECT = {
    "row through a byte pointer": "return (int)__ldg(" + MOTION_SAD_ROW + ");",
    "c-style row through bytes": "p = (const uint16_t *)(pic->data[0] + y * pic->stride[0]);",
    "stride divided by sizeof": "p = reinterpret_cast<const T *>(b) + y * (stride / sizeof(T));",
    "stride halved": "v = ((const uint16_t *)b)[y * (stride >> 1) + x];",
    "element stride name": "v = ((const float *)b)[y * stride_elems + x];",
    "byte pointer": "const uint8_t *b = (const uint8_t *)p + y * stride;",
    "auto from a byte cast": "auto *s = static_cast<const uint8_t *>(d); s + y * pic->stride[0];",
    "stride in a comment": "v = ((const uint16_t *)b)[x]; // + y * stride",
    "no stride": "v = ((const uint16_t *)b)[y * width + x];",
}


class ByteStrideContract(unittest.TestCase):
    def test_tree_has_no_byte_stride_on_a_wide_pointer(self) -> None:
        found = scan_tree()
        self.assertEqual([str(f) for f in found], [])

    def test_scan_is_not_vacuous(self) -> None:
        sources = gpu_sources()
        self.assertGreaterEqual(len(sources), MIN_SCANNED)
        for backend in sorted(BACKEND_DIRS):
            with self.subTest(backend=backend):
                self.assertTrue(any(backend in p.relative_to(SRC).parts for p in sources))
        suffixes = {p.suffix for p in sources}
        self.assertLessEqual({".cu", ".hip", ".cpp", ".metal", ".mm"}, suffixes)

    def test_planted_forms_are_found(self) -> None:
        for name, snippet in PLANTED.items():
            with self.subTest(form=name):
                self.assertEqual(len(scan_text("planted.cu", snippet)), 1, snippet)

    def test_correct_forms_pass(self) -> None:
        for name, snippet in CORRECT.items():
            with self.subTest(form=name):
                self.assertEqual(scan_text("correct.cu", snippet), [], snippet)

    def test_live_motion_kernel_back_to_upstream_form_fails(self) -> None:
        source = MOTION_SAD.read_text(encoding="utf-8")
        self.assertIn(MOTION_SAD_ROW, source)
        self.assertEqual(scan_text("motion_v2_score.cu", source), [])
        planted = scan_text(
            "motion_v2_score.cu", source.replace(MOTION_SAD_ROW, MOTION_SAD_UPSTREAM)
        )
        self.assertEqual([f.element for f in planted], ["T"])


if __name__ == "__main__":
    unittest.main()
