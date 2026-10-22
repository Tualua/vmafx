#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Every ssimulacra2 extractor refuses 4:0:0 input in init() with one check.

The colour conversion of SSIMULACRA 2 reads the U and V planes. A 4:0:0
picture has neither, so the CPU extractor and every twin must refuse it in
init(), before anything is allocated, through
``core/src/feature/ssimulacra2_pixel_format.h``. ``ssimulacra2_metal`` did
not: its init() discarded the pixel format, submit() read the U plane of a
4:0:0 picture through a NULL pointer, and the M4 Pro report of issue #2118
counted ``test_metal_ssimulacra2_parity`` failed although every case it
printed passed (T-METAL-SSIMULACRA2-YUV400-ACCEPTED-2026-10-05).

Checked on the sources, without a device (the Metal and GPU twins do not
build on every host):

* the init() each extractor registers calls ``vmaf_ss2_check_pixel_format``
  with ``pix_fmt`` before its first allocation, and does not discard
  ``pix_fmt``;
* the ADR-1324 context check of each twin uses ``vmaf_ss2_has_chroma``;
* no extractor spells its own 4:0:0 comparison: the header is the one
  implementation (HISS-19).

The arithmetic of the check is ``test_ssimulacra2_pixel_format``.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE = ROOT / "core" / "src" / "feature"
HEADER = FEATURE / "ssimulacra2_pixel_format.h"
EXTRACTORS = {
    "ssimulacra2": FEATURE / "ssimulacra2.c",
    "ssimulacra2_cuda": FEATURE / "cuda" / "ssimulacra2_cuda.c",
    "ssimulacra2_sycl": FEATURE / "sycl" / "ssimulacra2_sycl.cpp",
    "ssimulacra2_hip": FEATURE / "hip" / "ssimulacra2_hip.c",
    "ssimulacra2_metal": FEATURE / "metal" / "ssimulacra2_metal.mm",
}
TWINS = ("ssimulacra2_cuda", "ssimulacra2_sycl", "ssimulacra2_hip", "ssimulacra2_metal")

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
# The branch of ssimulacra2_hip.c's init() for a build without hipcc, which
# discards every argument and returns -ENOSYS.
NO_HIPCC_BRANCH = re.compile(r"#ifndef HAVE_HIPCC.*?#else", re.S)
CHECK_CALL = re.compile(r"\bvmaf_ss2_check_pixel_format\s*\(\s*pix_fmt\s*,")
DISCARDED = re.compile(r"\(\s*void\s*\)\s*pix_fmt\b")
OWN_COMPARISON = re.compile(r"[!=]=\s*VMAF_PIX_FMT_YUV400P\b|\bVMAF_PIX_FMT_YUV400P\s*[!=]=")
# The first allocation of each init(): host memory, a device context, a
# kernel lifecycle, a stream, a module, device buffers.
ALLOCATION = re.compile(
    r"\b(\w*alloc\w*|\w*_new|\w*lifecycle_init|hipStreamCreate\w*|\w*load_modules?|"
    r"\w*allocate)\s*\("
)


def code_of(source: str) -> str:
    return COMMENT.sub("", source)


def function_body(code: str, name: str) -> str | None:
    """The body of the function `name` (its definition, not a call)."""
    match = re.search(r"\b%s\s*\([^;{)]*\)\s*\{" % re.escape(name), code)
    if not match:
        return None
    depth = 0
    for index in range(match.end() - 1, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[match.end() : index]
    return None


def registered(code: str, field: str) -> str | None:
    match = re.search(r"\.%s\s*=\s*(\w+)" % field, code)
    return match.group(1) if match else None


def init_failures(name: str, code: str) -> list[str]:
    init = registered(code, "init")
    body = function_body(code, init) if init else None
    if body is None:
        return [f"{name}: no init() found"]
    body = NO_HIPCC_BRANCH.sub("", body)
    failures = []
    check = CHECK_CALL.search(body)
    if not check:
        return [f"{name}: {init}() does not call vmaf_ss2_check_pixel_format(pix_fmt, ...)"]
    allocation = ALLOCATION.search(body)
    if allocation and allocation.start() < check.start():
        failures.append(f"{name}: {init}() allocates ({allocation.group(1)}) before the check")
    if DISCARDED.search(body):
        failures.append(f"{name}: {init}() discards pix_fmt")
    return failures


def context_failures(name: str, code: str) -> list[str]:
    check = registered(code, "context_check")
    if check is None:
        return []
    body = function_body(code, check)
    if body is None or "vmaf_ss2_has_chroma(pix_fmt)" not in body:
        return [f"{name}: {check}() does not use vmaf_ss2_has_chroma(pix_fmt)"]
    return []


def contract_failures(sources: dict[str, str], header: str) -> list[str]:
    failures = []
    if len(OWN_COMPARISON.findall(code_of(header))) != 1:
        failures.append("ssimulacra2_pixel_format.h: not exactly one 4:0:0 comparison")
    for name, source in sources.items():
        code = code_of(source)
        failures += init_failures(name, code)
        if name in TWINS:
            failures += context_failures(name, code)
        if OWN_COMPARISON.search(code):
            failures.append(f"{name}: compares with VMAF_PIX_FMT_YUV400P itself")
    return failures


def read_sources() -> dict[str, str]:
    return {name: path.read_text(encoding="utf-8") for name, path in EXTRACTORS.items()}


class Ssimulacra2PixelFormatContract(unittest.TestCase):
    def setUp(self) -> None:
        self.sources = read_sources()
        self.header = HEADER.read_text(encoding="utf-8")

    def _edited(self, name: str, old: str, new: str) -> list[str]:
        sources = dict(self.sources)
        self.assertIn(old, sources[name], f"{name}: the planted edit no longer applies")
        sources[name] = sources[name].replace(old, new, 1)
        return contract_failures(sources, self.header)

    def _assert_detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in f for f in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(contract_failures(self.sources, self.header), [])

    def test_metal_init_without_the_check_is_detected(self) -> None:
        """The Metal twin as it was: init() discarded the pixel format."""
        failures = self._edited(
            "ssimulacra2_metal",
            'const int fmt_err = vmaf_ss2_check_pixel_format(pix_fmt, "ssimulacra2_metal");',
            "(void)pix_fmt; const int fmt_err = 0;",
        )
        self._assert_detected(failures, "ssimulacra2_metal: init_fex_metal() does not call")

    def test_check_after_allocation_is_detected(self) -> None:
        check = '    const int fmt_err = vmaf_ss2_check_pixel_format(pix_fmt, "ssimulacra2");\n'
        failures = self._edited(
            "ssimulacra2", check, "    void *early = aligned_malloc(64, 32);\n" + check
        )
        self._assert_detected(failures, "allocates (aligned_malloc) before the check")

    def test_private_comparison_is_detected(self) -> None:
        failures = self._edited(
            "ssimulacra2_cuda",
            "return (vmaf_ss2_has_chroma(pix_fmt) && w >= 8u",
            "return (pix_fmt != VMAF_PIX_FMT_YUV400P && w >= 8u",
        )
        self._assert_detected(failures, "ssimulacra2_cuda: compares with VMAF_PIX_FMT_YUV400P")
        self._assert_detected(failures, "check_context_cuda() does not use vmaf_ss2_has_chroma")

    def test_hip_check_in_the_no_hipcc_branch_only_is_detected(self) -> None:
        failures = self._edited(
            "ssimulacra2_hip",
            'const int fmt_err = vmaf_ss2_check_pixel_format(pix_fmt, "ssimulacra2_hip");',
            "const int fmt_err = 0;",
        )
        self._assert_detected(failures, "ssimulacra2_hip: init_fex_hip() does not call")


if __name__ == "__main__":
    unittest.main()
