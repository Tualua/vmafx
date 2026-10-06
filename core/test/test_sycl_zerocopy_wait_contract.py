#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The VA import fences its upload slot on the device (ADR-1769, C3).

Device-free: reads core/src/sycl/ and ffmpeg-patches/0005 only.

The FFmpeg ``libvmaf_sycl`` filter used to call ``vmaf_sycl_wait_compute()``
at the start of every frame. That host wait drained the primary and the
combined queue before the VA import overwrote the upload slot, and nothing
else protected the slot. The import now orders its writes on the device:

1. ``sycl_fence_slot_readers()`` takes the queue it barriers as a parameter.
   The host upload passes the copy queue; the VA import passes the primary
   queue, where the de-tile, the chroma de-interleave and the readback copies
   run.
2. The markers of a retiring slot cover the primary queue and every queue
   ``vmaf_sycl_create_compute_queue()`` made (the combined queue is one of
   them), so an extractor that ``n_subsample`` skipped is covered too.
3. ``vmaf_sycl_fence_import_slot()`` fences once per frame, at the start of
   ``vmaf_sycl_import_va_surface()``, before any write on any path.
4. ``vmaf_sycl_advance_frame()`` re-arms the fence outside its two
   load-bearing lines, and the deferred import frees keep room for a frame.

Each planted regression below is the construct the contract must catch.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SYCL = ROOT / "core" / "src" / "sycl"
COMMON = "common.cpp"
HEADER = "common.h"
DMABUF = "dmabuf_import.cpp"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
LOAD_BEARING = "state->cur_compute = state->cur_upload; state->cur_upload = 1 - state->cur_upload;"
MIN_PENDING_IMPORTS = 4


def _flat(text: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", text).split())


def _sources() -> dict[str, str]:
    return {name: (SYCL / name).read_text(encoding="utf-8") for name in (COMMON, HEADER, DMABUF)}


def _function_body(code: str, name: str) -> str:
    """Flattened text of the definition of `name` (brace-matched), or empty."""
    match = re.search(rf"\b{name}\([^;{{]*\) \{{", code)
    if not match:
        return ""
    depth = 0
    for index in range(match.end() - 1, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[match.start() : index + 1]
    return ""


def _fence_failures(code: str) -> list[str]:
    failures = []
    fence = _function_body(code, "sycl_fence_slot_readers")
    if "sycl::queue &barrier_q" not in fence or "barrier_q.ext_oneapi_submit_barrier(" not in fence:
        failures.append(f"{COMMON}: sycl_fence_slot_readers() does not barrier the queue it is given")
    if re.search(r"state->(?:copy_queue|queue)\.ext_oneapi_submit_barrier\(", fence):
        failures.append(f"{COMMON}: sycl_fence_slot_readers() barriers a fixed queue")
    markers = _function_body(code, "sycl_collect_last_events")
    if "sycl_collect_last_events(" not in fence or not re.search(
        r"for \([^)]*: state->compute_queues\)", markers
    ):
        failures.append(f"{COMMON}: the slot markers do not cover every compute queue")
    if "state->queue.ext_oneapi_get_last_event()" not in markers:
        failures.append(f"{COMMON}: the slot markers do not cover the primary queue")
    create = _function_body(code, "vmaf_sycl_create_compute_queue")
    if "state->compute_queues.push_back(*q);" not in create:
        failures.append(f"{COMMON}: vmaf_sycl_create_compute_queue() does not register its queue")
    register = _function_body(code, "vmaf_sycl_graph_register")
    if "vmaf_sycl_create_compute_queue(state)" not in register:
        failures.append(f"{COMMON}: the combined queue is not a registered compute queue")
    upload = _function_body(code, "vmaf_sycl_shared_frame_upload")
    if "sycl_fence_slot_readers(state, ui, state->copy_queue);" not in upload:
        failures.append(f"{COMMON}: the host upload no longer fences on the copy queue")
    return failures


def _import_slot_failures(code: str) -> list[str]:
    failures = []
    body = _function_body(code, "vmaf_sycl_fence_import_slot")
    if "sycl_fence_slot_readers(state, state->cur_upload, state->queue);" not in body:
        failures.append(
            f"{COMMON}: vmaf_sycl_fence_import_slot() does not fence the upload slot "
            "on the primary queue"
        )
    if "if (state->planes.import_slot_fenced)" not in body or (
        "state->planes.import_slot_fenced = true;" not in body
    ):
        failures.append(f"{COMMON}: vmaf_sycl_fence_import_slot() is not once per frame")
    if "catch (const sycl::exception" not in body or "return -EIO;" not in body:
        failures.append(f"{COMMON}: vmaf_sycl_fence_import_slot() lets a SYCL exception escape")
    advance = _function_body(code, "vmaf_sycl_advance_frame")
    if LOAD_BEARING not in advance:
        failures.append(f"{COMMON}: the load-bearing line pair of vmaf_sycl_advance_frame() moved")
    if "state->planes.import_slot_fenced = false;" not in advance:
        failures.append(f"{COMMON}: vmaf_sycl_advance_frame() does not re-arm the import fence")
    pending = re.search(r"static constexpr int MAX_PENDING_IMPORTS = (\d+);", code)
    if not pending or int(pending.group(1)) < MIN_PENDING_IMPORTS:
        failures.append(f"{COMMON}: MAX_PENDING_IMPORTS is below the imports of one frame")
    return failures


def _call_site_failures(sources: dict[str, str]) -> list[str]:
    failures = []
    if "int vmaf_sycl_fence_import_slot(VmafSyclState *state);" not in _flat(sources[HEADER]):
        failures.append(f"{HEADER}: vmaf_sycl_fence_import_slot() is not declared")
    code = _flat(sources[DMABUF])
    entry = _function_body(code, "vmaf_sycl_import_va_surface")
    fence = entry.find("vmaf_sycl_fence_import_slot(state)")
    body = entry.find("import_va_surface_body(")
    if fence < 0 or body < 0 or fence > body:
        failures.append(
            f"{DMABUF}: the VA import does not fence its slot before it writes the slot"
        )
    inner = _function_body(code, "import_va_surface_body")
    if "vmaf_sycl_fence_import_slot" in inner:
        failures.append(f"{DMABUF}: the fence moved behind the export and the readback paths")
    return failures


def _contract_failures(sources: dict[str, str]) -> list[str]:
    code = _flat(sources[COMMON])
    return _fence_failures(code) + _import_slot_failures(code) + _call_site_failures(sources)


def _replaced(name: str, old: str, new: str) -> list[str]:
    sources = _sources()
    if old not in sources[name]:
        raise AssertionError(f"{name}: `{old}` not found, the planted regression is stale")
    sources[name] = sources[name].replace(old, new, 1)
    return _contract_failures(sources)


class SlotFence(unittest.TestCase):
    """C3: device slot fence on the VA import (PERF-02, PERF-03)."""

    def _detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_fence_on_the_copy_queue_in_the_va_helper_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "sycl_fence_slot_readers(state, state->cur_upload, state->queue);",
            "sycl_fence_slot_readers(state, state->cur_upload, state->copy_queue);",
        )
        self._detected(failures, "on the primary queue")

    def test_fixed_barrier_queue_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "barrier_q.ext_oneapi_submit_barrier(",
            "state->copy_queue.ext_oneapi_submit_barrier(",
        )
        self._detected(failures, "barrier")

    def test_missing_compute_queue_loop_is_detected(self) -> None:
        sources = _sources()
        text = sources[COMMON]
        loop = re.search(r"for \([^)]*: state->compute_queues\)", text)
        self.assertIsNotNone(loop)
        sources[COMMON] = text.replace(loop.group(0), "if (false)", 1)
        self._detected(_contract_failures(sources), "every compute queue")

    def test_unregistered_compute_queue_is_detected(self) -> None:
        failures = _replaced(COMMON, "state->compute_queues.push_back(*q);", "")
        self._detected(failures, "does not register its queue")

    def test_fence_after_the_import_is_detected(self) -> None:
        sources = _sources()
        text = sources[DMABUF]
        call = re.search(r"\n[^\n]*vmaf_sycl_fence_import_slot\(state\)[^\n]*\n", text)
        self.assertIsNotNone(call)
        start = text.index('extern "C" int vmaf_sycl_import_va_surface(')
        end = text.index("\n}\n", start)
        body = text[start:end]
        body = body.replace("vmaf_sycl_fence_import_slot(state)", "0", 1)
        body += "\n    (void)vmaf_sycl_fence_import_slot(state);"
        sources[DMABUF] = text[:start] + body + text[end:]
        self._detected(_contract_failures(sources), "before it writes the slot")

    def test_fence_not_once_per_frame_is_detected(self) -> None:
        failures = _replaced(COMMON, "state->planes.import_slot_fenced = false;", "")
        self._detected(failures, "re-arm")

    def test_code_between_the_load_bearing_lines_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "    state->cur_upload = 1 - state->cur_upload;\n    state->frame_counter++;",
            "    state->planes.import_slot_fenced = false;\n"
            "    state->cur_upload = 1 - state->cur_upload;\n    state->frame_counter++;",
        )
        self._detected(failures, "load-bearing")

    def test_small_pending_import_ring_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "static constexpr int MAX_PENDING_IMPORTS = 4;",
            "static constexpr int MAX_PENDING_IMPORTS = 2;",
        )
        self._detected(failures, "MAX_PENDING_IMPORTS")


if __name__ == "__main__":
    unittest.main()
