#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every pthread function the C tree calls exists in the Windows shim.

MSVC builds get their threads from `core/src/compat/win32/pthread.h`, a small
shim over Win32 primitives. A call to a pthread function the shim does not
define compiles on every POSIX host and fails to link on Windows only
(`LNK2001`), which is how `test_async_extractor_thread_pool` kept the Windows
ARM64 MSVC build red from 2026-10-01 (`pthread_self`, `pthread_equal`).

A function may be used without being in the shim only when the build probes
for it and leaves the caller out where it is missing; those are listed in
`PROBED` with the Meson variable that guards them, and the test checks that the
guard is still in `core/test/meson.build`.

Device-free and compiler-free: reads the sources only.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

CORE = Path(__file__).resolve().parents[1]
SHIM = CORE / "src" / "compat" / "win32" / "pthread.h"
SCANNED = ("src", "test", "tools")
SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
CALL = re.compile(r"\b(pthread_[a-z_]+)\s*\(")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

# function -> (file that may call it, Meson guard that keeps the file out of a
# build without the function)
PROBED = {
    "pthread_cond_timedwait": ("test/test_thread_pool_backpressure.c", "has_cond_timedwait"),
}


def shim_functions(text: str) -> set[str]:
    return set(CALL.findall(COMMENT.sub("", text)))


def calls_outside_shim(root: Path) -> dict[str, set[str]]:
    """Map each called pthread function to the files (relative to core/) calling it."""
    found: dict[str, set[str]] = {}
    for sub in SCANNED:
        for path in sorted((root / sub).rglob("*")):
            if path.suffix not in SUFFIXES or "compat/win32" in path.as_posix():
                continue
            code = COMMENT.sub("", path.read_text(encoding="utf-8", errors="replace"))
            for name in CALL.findall(code):
                found.setdefault(name, set()).add(path.relative_to(root).as_posix())
    return found


def missing_from_shim(root: Path) -> list[str]:
    provided = shim_functions(SHIM.read_text(encoding="utf-8"))
    missing = []
    for name, files in sorted(calls_outside_shim(root).items()):
        if name in provided:
            continue
        allowed = PROBED.get(name, ("", ""))[0]
        for file in sorted(files):
            if file != allowed:
                missing.append(f"{file}: {name}")
    return missing


class Win32PthreadShimContract(unittest.TestCase):
    def test_every_called_pthread_function_is_in_the_shim(self) -> None:
        self.assertEqual(missing_from_shim(CORE), [])

    def test_probed_functions_keep_their_meson_guard(self) -> None:
        meson = (CORE / "test" / "meson.build").read_text(encoding="utf-8")
        for name, (file, guard) in PROBED.items():
            target = Path(file).stem
            block = re.search(rf"if {guard}\n\s+{target} = executable\(", meson)
            self.assertIsNotNone(block, f"{name}: {target} is no longer built under 'if {guard}'")

    def test_a_call_in_a_comment_is_not_a_call(self) -> None:
        self.assertEqual(shim_functions("/* pthread_self() */ // pthread_equal(a, b)\n"), set())

    def test_an_unshimmed_call_is_reported(self) -> None:
        code = "static void f(void) { (void)pthread_self(); }\n"
        self.assertEqual(set(CALL.findall(COMMENT.sub("", code))), {"pthread_self"})
        self.assertNotIn("pthread_self", shim_functions(SHIM.read_text(encoding="utf-8")))


if __name__ == "__main__":
    unittest.main()
