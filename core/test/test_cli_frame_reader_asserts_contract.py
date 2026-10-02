#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the invariants of the CLI's read-ahead (`FrameReader`) as assertions.

`core/tools/vmaf.cpp` reads each input on its own thread, a bounded number of
frames ahead of the scoring loop. The ring's invariants (the slot count never
exceeds the depth, a reader is started once, a picture pointer is never null)
are `assert()`s: a violated one stops a debug build at the line that broke it.

On 2026-10-02 a lint cleanup replaced all seven by early returns, because
clang-tidy 22 on a glibc 2.44 host reports `misc-static-assert` /
`cert-dcl03-c` for every runtime `assert()` in a C++ translation unit (glibc's
macro expands to `expr ? void (1 ? 1 : bool (expr)) : __assert_fail(...)`, and
the check takes the constant arm for a constant condition). The early returns
turned a broken invariant into a silently dropped frame or a silently ended
stream. This contract fails when an invariant stops being asserted.

Device-free: reads the source only.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "tools" / "vmaf.cpp"

# function name -> the conditions it must assert
INVARIANTS = {
    "release_fetched_picture": ("pic != nullptr",),
    "FrameReader::start": ("!threaded_", "!thread_.joinable()"),
    "FrameReader::wait_for_free_slot": ("count_ <= kReadaheadDepth",),
    "FrameReader::publish": ("count_ < kReadaheadDepth",),
    "FrameReader::next": ("pic != nullptr",),
    "FrameReader::request_stop": ("count_ <= kReadaheadDepth",),
}


def function_body(text: str, name: str) -> str:
    """Return the body of the definition of ``name`` (brace-matched)."""
    match = re.search(rf"^[\w:<>\s\*&]*\b{re.escape(name)}\([^;{{]*\)\s*\n?{{", text, re.M)
    if match is None:
        raise AssertionError(f"definition of {name}() not found in {SOURCE.name}")
    depth, start = 0, match.end() - 1
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unbalanced braces after {name}()")


def missing_assertions(text: str) -> list[str]:
    """List ``function: condition`` for every invariant that is not asserted."""
    missing = []
    for name, conditions in INVARIANTS.items():
        body = function_body(text, name)
        for condition in conditions:
            if f"assert({condition});" not in body:
                missing.append(f"{name}: assert({condition})")
    return missing


class FrameReaderAssertsContract(unittest.TestCase):
    def test_every_invariant_is_asserted(self) -> None:
        text = SOURCE.read_text(encoding="utf-8")
        self.assertEqual(missing_assertions(text), [])
        self.assertIn("#include <cassert>", text)

    def test_an_early_return_in_place_of_an_assertion_is_reported(self) -> None:
        text = SOURCE.read_text(encoding="utf-8")
        planted = text.replace(
            "    assert(!threaded_);\n    assert(!thread_.joinable());\n",
            "    if (threaded_ || thread_.joinable())\n        return;\n",
        )
        self.assertNotEqual(planted, text)
        self.assertEqual(
            missing_assertions(planted),
            [
                "FrameReader::start: assert(!threaded_)",
                "FrameReader::start: assert(!thread_.joinable())",
            ],
        )

    def test_a_folded_condition_in_place_of_an_assertion_is_reported(self) -> None:
        text = SOURCE.read_text(encoding="utf-8")
        planted = text.replace(
            "        if (!stop_) {\n            assert(count_ < kReadaheadDepth);\n",
            "        if (!stop_ && count_ < kReadaheadDepth) {\n",
        )
        self.assertNotEqual(planted, text)
        self.assertEqual(
            missing_assertions(planted),
            ["FrameReader::publish: assert(count_ < kReadaheadDepth)"],
        )


if __name__ == "__main__":
    unittest.main()
