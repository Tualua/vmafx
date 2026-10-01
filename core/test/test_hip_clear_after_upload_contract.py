#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin the order of a HIP frame: upload, then clear, then kernels (ADR-1427).

On a gfx1036 an accumulator clear (``hipMemsetAsync``) that is queued ahead of
the frame's plane upload has no effect in the first context of a process that
needs larger planes than the contexts before it. The kernels then add onto the
sums recycled device memory holds: ``adm_hip`` returned a NaN numerator,
``float_moment_hip`` moments 3 % too large, ``vif_hip`` scores 0.02 too low.
A clear queued after the upload works.

So no function of a HIP twin may queue a clear and upload afterwards. The rule
follows calls inside a file: a helper that clears counts as a clear where it
is called, a helper that uploads as an upload.

Device-free: reads the sources only. ``test_hip_first_frame_clear`` checks
the first frame of every twin on a device; this file also pins that meson
builds that test once per twin of its table.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRS = (ROOT / "core" / "src" / "feature" / "hip", ROOT / "core" / "src" / "hip")
DEVICE_TEST = ROOT / "core" / "test" / "test_hip_first_frame_clear.c"
MESON = ROOT / "core" / "test" / "meson.build"

# The calls that upload a frame's planes.
UPLOADS = (
    "vmaf_hip_plane_source_acquire",
    "vmaf_hip_plane_source_acquire_luma",
    "vmaf_hip_picture_upload",
    "vmaf_hip_picture_upload_staged",
)
# The calls that clear device memory. ``hipMemset`` is asynchronous on device
# memory too (hipamd ``ihipMemset()``).
CLEARS = ("hipMemsetAsync", "hipMemset", "hipMemsetD8Async", "hipMemsetD32Async")
# Helpers calling helpers: the deepest chain in the tree is three calls.
CALL_DEPTH = 6

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
SIGNATURE = re.compile(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$")
TWIN_ROW = re.compile(r'^\s*\{"(\w+_hip)",', re.M)
MESON_TWINS = re.compile(r"hip_first_frame_twins = \[(.*?)\]", re.S)


def _code(source: str) -> str:
    """The source without comments and string contents."""
    return STRING.sub('""', COMMENT.sub(" ", source))


def _functions(source: str) -> dict[str, str]:
    """Name and body of every function defined at file scope."""
    code = _code(source)
    functions: dict[str, str] = {}
    depth = 0
    start = 0
    name = ""
    for at, char in enumerate(code):
        if char == "{":
            if depth == 0:
                match = SIGNATURE.search(code, max(0, at - 600), at)
                name = match.group(1) if match else ""
                start = at
            depth += 1
        elif char == "}" and depth > 0:
            depth -= 1
            if depth == 0 and name:
                functions[name] = code[start : at + 1]
    return functions


def _positions(body: str, names: set[str]) -> list[int]:
    """Where ``body`` calls one of ``names``."""
    found: list[int] = []
    for name in names:
        pattern = r"(?<![A-Za-z0-9_])" + re.escape(name) + r"\s*\("
        found.extend(match.start() for match in re.finditer(pattern, body))
    return found


def _callers(functions: dict[str, str], seeds: tuple[str, ...]) -> set[str]:
    """``seeds`` and every function of the file that reaches one of them."""
    reached = set(seeds)
    for _ in range(CALL_DEPTH):
        grown = {name for name, body in functions.items() if _positions(body, reached)}
        if grown <= reached:
            break
        reached |= grown
    return reached


def _file_failures(label: str, source: str) -> list[str]:
    """Functions of one file that clear and upload afterwards."""
    functions = _functions(source)
    uploads = _callers(functions, UPLOADS)
    clears = _callers(functions, CLEARS)
    failures: list[str] = []
    for name, body in functions.items():
        # A helper that does both is one call for its caller, in the right
        # order inside; what matters there is the helper's own body.
        cleared = _positions(body, clears - uploads)
        uploaded = _positions(body, uploads)
        if cleared and uploaded and min(cleared) < max(uploaded):
            failures.append(f"{label}: {name}() queues a clear ahead of an upload")
    return failures


def _sources() -> dict[str, str]:
    sources: dict[str, str] = {}
    for directory in SOURCE_DIRS:
        for path in sorted(directory.glob("*.c")):
            sources[str(path.relative_to(ROOT))] = path.read_text(encoding="utf-8")
    return sources


def _contract_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    for label, source in sources.items():
        failures.extend(_file_failures(label, source))
    return failures


def _table_twins() -> list[str]:
    return TWIN_ROW.findall(DEVICE_TEST.read_text(encoding="utf-8"))


def _meson_twins() -> list[str]:
    match = MESON_TWINS.search(MESON.read_text(encoding="utf-8"))
    return re.findall(r"'(\w+)'", match.group(1)) if match else []


class ClearAfterUploadContract(unittest.TestCase):
    def plant(self, label: str, upload: str, clear: str) -> None:
        """A clear queued right ahead of ``upload`` in ``label`` is reported."""
        sources = _sources()
        self.assertIn(upload, sources[label])
        sources[label] = sources[label].replace(upload, clear + "\n    " + upload, 1)
        failures = _contract_failures(sources)
        self.assertTrue(any(item.startswith(label + ":") for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        sources = _sources()
        self.assertIn("core/src/feature/hip/float_moment_hip.c", sources)
        self.assertEqual(_contract_failures(sources), [])

    def test_parser_finds_the_frame_functions(self) -> None:
        sources = _sources()
        for label, name in (
            ("core/src/feature/hip/float_moment_hip.c", "moment_hip_launch"),
            ("core/src/feature/hip/float_psnr_hip.c", "float_psnr_hip_launch"),
            ("core/src/feature/hip/integer_adm_hip.c", "adm_hip_stage_luma"),
            ("core/src/hip/shared_frame.c", "vmaf_hip_plane_source_acquire"),
        ):
            self.assertIn(name, _functions(sources[label]), label)

    def test_float_moment_clear_ahead_of_the_upload_is_detected(self) -> None:
        # The pre-ADR-1427 frame: clear, upload, kernel.
        self.plant(
            "core/src/feature/hip/float_moment_hip.c",
            "int err = vmaf_hip_plane_source_acquire_luma(",
            "(void)hipMemsetAsync(s->rb.device, 0, sums_bytes, str);",
        )

    def test_vif_clear_ahead_of_the_upload_is_detected(self) -> None:
        self.plant(
            "core/src/feature/hip/integer_vif_hip.c",
            "int err = vmaf_hip_plane_source_acquire_luma(",
            "(void)hipMemsetAsync(s->accum_dev, 0, sizeof(vif_accums_hip) * 4u, s->str);",
        )

    def test_float_psnr_clear_ahead_of_the_upload_is_detected(self) -> None:
        self.plant(
            "core/src/feature/hip/float_psnr_hip.c",
            "int err = vmaf_hip_plane_source_acquire_luma(",
            "(void)hipMemsetAsync(s->rb.device, 0, sizeof(float), str);",
        )

    def test_adm_clear_ahead_of_the_upload_helper_is_detected(self) -> None:
        # The upload is inside adm_hip_stage_luma(); the pre-ADR-1423 frame
        # cleared ahead of that call.
        self.plant(
            "core/src/feature/hip/integer_adm_hip.c",
            "int err = adm_hip_stage_luma(s, frame, ref_pic, dis_pic);",
            "(void)hipMemsetAsync(buf->tmp_res, 0, sizeof(int64_t), s->str);",
        )

    def test_null_stream_clear_ahead_of_the_upload_is_detected(self) -> None:
        self.plant(
            "core/src/feature/hip/float_moment_hip.c",
            "int err = vmaf_hip_plane_source_acquire_luma(",
            "(void)hipMemset(s->rb.device, 0, sums_bytes);",
        )

    def test_clearing_helper_ahead_of_the_upload_is_detected(self) -> None:
        source = (
            "static int reset(S *s)\n{\n    return hipMemsetAsync(s->sums, 0, 8u, s->str);\n}\n"
            "static int frame(S *s, P *pic)\n{\n    int err = reset(s);\n"
            "    if (!err)\n        err = vmaf_hip_picture_upload(pic, 1u, s->str);\n"
            "    return err;\n}\n"
        )
        self.assertEqual(
            _file_failures("planted.c", source),
            ["planted.c: frame() queues a clear ahead of an upload"],
        )

    def test_upload_then_clear_is_accepted(self) -> None:
        source = (
            "static int frame(S *s, P *pic)\n{\n"
            "    int err = vmaf_hip_picture_upload(pic, 1u, s->str);\n"
            "    if (!err)\n        err = hipMemsetAsync(s->sums, 0, 8u, s->str);\n"
            "    return err;\n}\n"
            "static int submit(S *s, P *pic)\n{\n    return frame(s, pic);\n}\n"
        )
        self.assertEqual(_file_failures("planted.c", source), [])

    def test_meson_builds_the_device_test_once_per_twin(self) -> None:
        table = _table_twins()
        self.assertGreaterEqual(len(table), 14)
        self.assertEqual(_meson_twins(), table)


if __name__ == "__main__":
    unittest.main()
