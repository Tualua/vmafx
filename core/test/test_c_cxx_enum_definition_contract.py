#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""An enum that C and C++ translation units both see has one size (ADR-1470).

C++ can fix an enum's underlying type (``enum E : unsigned char``); C on the
fork's required toolchains cannot (MSVC does not take the C23 spelling,
ADR-1138). A header that spells a fixed type for C++ only therefore gives the
enum one size in C++ and ``int``'s size in C. Passed by value across
``extern "C"``, stored in a shared struct or reached through a pointer, the two
languages then read different bytes: ``VmafSyclDispatchStrategy`` returned
through an 8-bit register to a C caller that read 32 broke
``test_gpu_dispatch_runtime``.

The rule this test holds, for every header under ``core/`` that a C
translation unit includes (directly or through other headers):

- an enum has no fixed underlying type narrower or wider than ``int``; and
- one whose C++ spelling fixes ``unsigned int`` (the three ABI-pinned enums of
  ``model.h`` and ``luminance_tools.h``) carries a ``UINT_MAX`` enumerator, so
  the C enum cannot be smaller than that either.

Device-free and compiler-free. The planted regressions are the definitions
this rule removed or would reject.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "core"
SCANNED = ("include", "src", "tools", "test")
C_SUFFIXES = (".c", ".m")
HEADER_SUFFIXES = (".h",)
# Vendored sources keep their upstream text.
SKIPPED_PARTS = ("third_party", "subprojects")

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
# `enum Name : type {` -- a fixed underlying type; `enum class` included.
FIXED_ENUM = re.compile(r"\benum\s+(?:class\s+|struct\s+)?(\w+)\s*:\s*([\w:\s]+?)\s*\{")
INT_SIZED = {"int", "unsigned", "unsigned int", "signed int", "int32_t", "uint32_t"}
INT_SIZED |= {f"std::{name}" for name in ("int32_t", "uint32_t")}


def _code(text: str) -> str:
    return COMMENT.sub(" ", text)


def _files(suffixes: tuple[str, ...]) -> list[Path]:
    found = []
    for top in SCANNED:
        for path in sorted((CORE / top).rglob("*")):
            if path.suffix in suffixes and not any(part in SKIPPED_PARTS for part in path.parts):
                found.append(path)
    return found


def _sources() -> dict[str, str]:
    """Relative path -> text of every C source and every header under core/."""
    return {
        path.relative_to(ROOT).as_posix(): path.read_text(encoding="utf-8", errors="replace")
        for path in _files(C_SUFFIXES + HEADER_SUFFIXES)
    }


def _headers_c_sees(sources: dict[str, str]) -> set[str]:
    """Headers reachable through quoted includes from a C translation unit.

    An include is matched by its path suffix against every header, which is
    how the build's include directories resolve it; a name two headers share
    counts for both.
    """
    headers = [name for name in sources if name.endswith(HEADER_SUFFIXES)]
    seen: set[str] = set()
    queue = [name for name in sources if name.endswith(C_SUFFIXES)]
    while queue:
        current = queue.pop()
        for included in INCLUDE.findall(sources[current]):
            tail = "/" + included.lstrip("./")
            for header in headers:
                if header not in seen and ("/" + header).endswith(tail):
                    seen.add(header)
                    queue.append(header)
    return seen


def _enumerators(code: str, start: int) -> str:
    """Text between the braces of the enum whose `{` precedes `start`."""
    return code[start : code.find("}", start)]


def _contract_failures(sources: dict[str, str]) -> list[str]:
    failures = []
    for name in sorted(_headers_c_sees(sources)):
        code = _code(sources[name])
        for match in FIXED_ENUM.finditer(code):
            enum, base = match.group(1), " ".join(match.group(2).split())
            if base not in INT_SIZED:
                failures.append(
                    f"{name}: enum {enum} has the underlying type `{base}` in C++; a C "
                    "translation unit that includes this header gives it int's size"
                )
            elif base != "int" and "UINT_MAX" not in _enumerators(code, match.end()):
                failures.append(
                    f"{name}: enum {enum} is `{base}` in C++ and has no UINT_MAX enumerator "
                    "that holds the C enum to the same size"
                )
    return failures


def _replaced(name: str, old: str, new: str) -> list[str]:
    sources = _sources()
    if old not in sources[name]:
        raise AssertionError(f"{name}: `{old}` not found, the planted regression is stale")
    sources[name] = sources[name].replace(old, new, 1)
    return _contract_failures(sources)


NONFINITE = "core/src/feature/nonfinite_score.h"
MODEL = "core/src/model.h"
DISPATCH = "core/src/sycl/dispatch_strategy.h"


class CCxxEnumDefinitionContract(unittest.TestCase):
    def _detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_headers_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_the_scan_reaches_the_shared_headers(self) -> None:
        # Guards the include walk: a walk that found nothing would pass.
        seen = _headers_c_sees(_sources())
        for name in (NONFINITE, MODEL, "core/src/feature/luminance_tools.h", DISPATCH):
            self.assertIn(name, seen)
        self.assertGreater(len(seen), 100)

    def test_the_abi_pinned_enums_are_recognised(self) -> None:
        # The three dual spellings that are size-compatible stay allowed.
        code = _code(_sources()[MODEL])
        enums = {m.group(1): " ".join(m.group(2).split()) for m in FIXED_ENUM.finditer(code)}
        self.assertEqual(
            enums,
            {"VmafModelType": "unsigned int", "VmafModelNormalizationType": "unsigned int"},
        )

    def test_cxx_only_narrow_type_is_detected(self) -> None:
        # nonfinite_score.h before ADR-1470.
        failures = _replaced(
            NONFINITE,
            "typedef enum VmafVifNameSet {",
            "#ifdef __cplusplus\nenum VmafVifNameSet : unsigned char {\n#else\n"
            "typedef enum VmafVifNameSet {\n#endif",
        )
        self._detected(failures, "enum VmafVifNameSet has the underlying type `unsigned char`")

    def test_narrow_return_type_enum_is_detected(self) -> None:
        # The definition that broke test_gpu_dispatch_runtime.
        failures = _replaced(
            DISPATCH,
            "typedef enum {",
            "#ifdef __cplusplus\nenum VmafSyclDispatchStrategy : uint8_t {\n#else\ntypedef enum {\n#endif",
        )
        self._detected(failures, "`uint8_t`")

    def test_scoped_enum_with_a_narrow_type_is_detected(self) -> None:
        failures = _replaced(
            NONFINITE,
            "typedef enum VmafVifNameSet {",
            "enum class VmafOther : std::uint16_t { A };\ntypedef enum VmafVifNameSet {",
        )
        self._detected(failures, "`std::uint16_t`")

    def test_wider_type_is_detected(self) -> None:
        failures = _replaced(
            MODEL,
            "enum VmafModelType : unsigned int {",
            "enum VmafModelType : unsigned long long {",
        )
        self._detected(failures, "`unsigned long long`")

    def test_unpinned_unsigned_enum_is_detected(self) -> None:
        failures = _replaced(MODEL, "    VMAF_MODEL_TYPE_ABI_UINT_MAX = UINT_MAX,\n", "")
        self._detected(failures, "no UINT_MAX enumerator")

    def test_cxx_only_header_may_fix_a_narrow_type(self) -> None:
        # A header no C translation unit includes is outside the rule.
        sources = _sources()
        sources["core/src/only_cxx_example.h"] = "enum Narrow : unsigned char { A };\n"
        self.assertEqual(_contract_failures(sources), [])


if __name__ == "__main__":
    unittest.main()
