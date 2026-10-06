#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Three properties of the SYCL runtime that a lint cleanup once removed.

Device-free: reads core/src/sycl/ only.

1. ``VmafSyclState`` is initialised with the two queues of the selected
   device in the new-expression itself and is never default-constructed. A
   default-constructed ``sycl::queue`` selects the DEFAULT device and creates
   a queue there (and a context, when that device is on another platform), so
   ``new VmafSyclState`` followed by assignments touches a device the caller
   did not select. Measured on a host with a Level Zero and an OpenCL view of
   an Arc A380: ``--sycl_device 1`` created two contexts and five queues over
   both devices that way, one context and three queues on the selected device
   with the queues passed in.
2. A type that a C and a C++ translation unit both read has one definition.
   ``enum X : uint8_t`` under ``__cplusplus`` next to a plain C ``enum X``
   makes a C caller pass an int where the C++ callee reads a byte
   (``libvmaf.c`` passes ``enum VmafSyclPoolMethod`` to
   ``vmaf_sycl_picture_pool_init()``; the same pattern in
   ``dispatch_strategy.h`` broke ``test_gpu_dispatch_runtime``).
3. The de-tile submits of ``vmaf_sycl_import_va_surface()`` run under a
   ``try``: the function is ``extern "C"``, and a synchronous
   ``sycl::exception`` that crosses it ends the process. The two Level Zero
   descriptors of the DMA-BUF import are initialised where they are declared,
   so a field a newer header adds is zero.

4. The host phase timers (``VMAF_SYCL_TIMING``) stay opt-in and complete: the
   ``[vmaf-sycl] phases:`` line prints only under ``state->extractor_timing``,
   each of the three waits records its phase after the wait succeeded, the
   import records around the whole VA import, and the phase enum has no
   C++-only underlying type.

Each planted regression below is the construct the cleanup introduced.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SYCL = ROOT / "core" / "src" / "sycl"
COMMON = "common.cpp"
DMABUF = "dmabuf_import.cpp"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
MEMBERS = ["sycl::queue queue", "sycl::queue copy_queue"]
CONSTRUCTION = "new VmafSyclState{.queue = std::move(q), .copy_queue = std::move(cq)};"
# An enum with a fixed underlying type, and any type defined only for C++.
FIXED_ENUM = re.compile(r"\benum\s+(?:class\s+)?\w+\s*:\s*[\w:\s]+\{")
CXX_ONLY_TYPE = re.compile(r"#ifdef __cplusplus\s+(?:enum|struct|using)\b")
DESCRIPTORS = (
    "const ze_external_memory_import_fd_t import_desc = { "
    ".stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD,",
    "const ze_device_mem_alloc_desc_t alloc_desc = { "
    ".stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,",
)
PHASE_WAITS = (
    "vmaf_sycl_queue_wait",
    "vmaf_sycl_combined_queue_wait",
    "vmaf_sycl_graph_wait",
)
DETILE_CALLS = ("detile_linear(", "detile_tile4(", "detile_y_tiled(")


def _flat(text: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", text).split())


def _sources() -> dict[str, str]:
    paths = [*sorted(SYCL.glob("*.h")), SYCL / COMMON, SYCL / DMABUF]
    return {path.name: path.read_text(encoding="utf-8") for path in paths}


def _struct_body(code: str, name: str) -> str:
    """Flattened text between the braces of `struct name`, nested types removed."""
    start = code.find(f"struct {name} {{")
    if start < 0:
        return ""
    depth = 0
    kept = []
    for char in code[start + len(f"struct {name} ") :]:
        if char == "{":
            depth += 1
            continue
        if char == "}":
            depth -= 1
            if depth == 0:
                break
            continue
        if depth == 1:
            kept.append(char)
    return " ".join("".join(kept).split())


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


def _state_failures(common: str) -> list[str]:
    code = _flat(common)
    failures = []
    body = _struct_body(code, "VmafSyclState")
    # Data members in declaration order: not the constants, not the nested types.
    members = [
        item.strip()
        for item in body.split(";")
        if item.strip() and not item.strip().startswith(("static ", "struct "))
    ]
    if members[:2] != MEMBERS:
        failures.append(f"{COMMON}: the two queues are not the first members of VmafSyclState")
    if re.search(r"\bVmafSyclState\(", body):
        failures.append(
            f"{COMMON}: VmafSyclState has a constructor; it is initialised as an aggregate"
        )
    constructions = re.findall(r"\bnew VmafSyclState\b[^;]*;", code)
    if constructions != [CONSTRUCTION]:
        failures.append(
            f"{COMMON}: the state is not initialised with the selected queues in its "
            "new-expression (a default-constructed queue touches the default device)"
        )
    return failures


def _header_failures(sources: dict[str, str]) -> list[str]:
    failures = []
    for name, text in sources.items():
        if not name.endswith(".h"):
            continue
        code = COMMENT.sub(" ", text)
        if FIXED_ENUM.search(code):
            failures.append(
                f"{name}: an enum with a fixed underlying type in a header C also reads"
            )
        if CXX_ONLY_TYPE.search(code):
            failures.append(f"{name}: a type defined once for C++ and once for C")
    return failures


def _dmabuf_failures(dmabuf: str) -> list[str]:
    code = _flat(dmabuf)
    failures = [
        f"{DMABUF}: `{piece}` is gone; a descriptor is not initialised where it is declared"
        for piece in DESCRIPTORS
        if piece not in code
    ]
    dispatch = _function_body(code, "dispatch_detile")
    guarded = dispatch[dispatch.find("try {") : dispatch.find("} catch (const sycl::exception")]
    if not dispatch or any(call not in guarded for call in DETILE_CALLS):
        failures.append(f"{DMABUF}: a de-tile submit is outside the try of dispatch_detile()")
    for name in ("vmaf_sycl_import_va_surface", "import_va_surface_body"):
        surface = _function_body(code, name)
        if any(call in surface for call in DETILE_CALLS) or "parallel_for" in surface:
            failures.append(f"{DMABUF}: vmaf_sycl_import_va_surface() submits a kernel itself")
    return failures


def _phase_failures(sources: dict[str, str]) -> list[str]:
    code = _flat(sources[COMMON])
    failures = []
    printer = _function_body(code, "vmaf_sycl_print_timing")
    if not re.search(r"if \(state->extractor_timing\) \{ print_phase_timing\(state\);", printer):
        failures.append(f"{COMMON}: the phases line is not printed only under extractor_timing")
    for name in PHASE_WAITS:
        body = _function_body(code, name)
        wait = body.find("wait_and_throw()")
        if wait < 0 or body.find("vmaf_sycl_phase_record(", wait) < 0:
            failures.append(f"{COMMON}: {name}() does not record its phase after the wait")
    entry = _function_body(_flat(sources[DMABUF]), "vmaf_sycl_import_va_surface")
    if "VMAF_SYCL_PHASE_IMPORT" not in entry:
        failures.append(f"{DMABUF}: the VA import does not record VMAF_SYCL_PHASE_IMPORT")
    if "enum VmafSyclPhase {" not in _flat(sources["common.h"]):
        failures.append("common.h: enum VmafSyclPhase is missing or has an underlying type")
    return failures


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return (
        _state_failures(sources[COMMON])
        + _header_failures(sources)
        + _dmabuf_failures(sources[DMABUF])
        + _phase_failures(sources)
    )


def _replaced(name: str, old: str, new: str) -> list[str]:
    sources = _sources()
    if old not in sources[name]:
        raise AssertionError(f"{name}: `{old}` not found, the planted regression is stale")
    sources[name] = sources[name].replace(old, new, 1)
    return _contract_failures(sources)


class SyclRuntimeContract(unittest.TestCase):
    def _detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_default_constructed_state_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "auto *s = new VmafSyclState{.queue = std::move(q), .copy_queue = std::move(cq)};",
            "auto *s = new VmafSyclState;\n        s->queue = std::move(q);\n"
            "        s->copy_queue = std::move(cq);",
        )
        self._detected(failures, "not initialised with the selected queues")

    def test_value_initialised_state_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "new VmafSyclState{.queue = std::move(q), .copy_queue = std::move(cq)};",
            "new VmafSyclState{};",
        )
        self._detected(failures, "not initialised with the selected queues")

    def test_queue_member_moved_down_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "    sycl::queue queue;      // primary queue: VA import, misc ops; immediate cmdlists (ADR-1596)\n",
            "    void *first = nullptr;\n"
            "    sycl::queue queue;      // primary queue: VA import, misc ops; immediate cmdlists (ADR-1596)\n",
        )
        self._detected(failures, "not the first members")

    def test_fixed_underlying_enum_is_detected(self) -> None:
        failures = _replaced(
            "picture_sycl.h", "enum VmafSyclPoolMethod {", "enum VmafSyclPoolMethod : uint8_t {"
        )
        self._detected(failures, "fixed underlying type")

    def test_cxx_only_enum_definition_is_detected(self) -> None:
        failures = _replaced(
            "picture_sycl.h",
            "enum VmafSyclPoolMethod {",
            "#ifdef __cplusplus\nenum VmafSyclPoolMethodCxx {\n};\n#endif\nenum VmafSyclPoolMethod {",
        )
        self._detected(failures, "once for C++ and once for C")

    def test_cxx_only_alias_is_detected(self) -> None:
        failures = _replaced(
            "dmabuf_import.h",
            "typedef struct VmafSyclState VmafSyclState;",
            "#ifdef __cplusplus\nusing VmafSyclState = struct VmafSyclState;\n#else\n"
            "typedef struct VmafSyclState VmafSyclState;\n#endif",
        )
        self._detected(failures, "once for C++ and once for C")

    def test_uninitialised_descriptor_is_detected(self) -> None:
        failures = _replaced(
            DMABUF,
            "const ze_device_mem_alloc_desc_t alloc_desc = {",
            "ze_device_mem_alloc_desc_t alloc_desc; alloc_desc = {",
        )
        self._detected(failures, "not initialised where it is declared")

    def test_unguarded_detile_submit_is_detected(self) -> None:
        failures = _replaced(
            DMABUF, "    try {\n        if (linear) {", "    {\n        if (linear) {"
        )
        self._detected(failures, "outside the try")

    def test_submit_in_the_c_entry_point_is_detected(self) -> None:
        failures = _replaced(
            DMABUF,
            "    return import_exported_surface(args, desc, &cplan);\n",
            "    (void)detile_tile4(q, target_buf, imported_ptr, y_offset, y_pitch, row_bytes, h, bpc);\n"
            "    return import_exported_surface(args, desc, &cplan);\n",
        )
        self._detected(failures, "submits a kernel itself")


class PhaseTimers(unittest.TestCase):
    """Host phase timers behind VMAF_SYCL_TIMING (Phase 13, PERF-01)."""

    def _detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in f for f in failures), failures)

    def test_phase_timers_satisfy_the_contract(self) -> None:
        self.assertEqual(_phase_failures(_sources()), [])

    def test_unconditional_phases_print_is_detected(self) -> None:
        failures = _replaced(
            COMMON,
            "if (state->extractor_timing) {\n        print_phase_timing(state);",
            "{\n        print_phase_timing(state);",
        )
        self._detected(failures, "only under extractor_timing")

    def test_wait_without_record_is_detected(self) -> None:
        failures = _drop_record(COMMON, "vmaf_sycl_graph_wait")
        self._detected(failures, "vmaf_sycl_graph_wait() does not record")

    def test_import_without_record_is_detected(self) -> None:
        failures = _replaced(DMABUF, "VMAF_SYCL_PHASE_IMPORT", "VMAF_SYCL_PHASE_COUNT")
        self._detected(failures, "does not record VMAF_SYCL_PHASE_IMPORT")

    def test_fixed_underlying_phase_enum_is_detected(self) -> None:
        failures = _replaced("common.h", "enum VmafSyclPhase {", "enum VmafSyclPhase : int {")
        self._detected(failures, "enum VmafSyclPhase")


def _drop_record(name: str, function: str) -> list[str]:
    """Contract failures with the phase record removed from `function`."""
    sources = _sources()
    text = sources[name]
    start = text.index(f'extern "C" int {function}(')
    end = text.index("\n}\n", start)
    body = text[start:end].replace("vmaf_sycl_phase_record(", "(void)sizeof(")
    sources[name] = text[:start] + body + text[end:]
    return _contract_failures(sources)


if __name__ == "__main__":
    unittest.main()
