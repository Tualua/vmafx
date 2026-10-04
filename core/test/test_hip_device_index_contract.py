#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin how a HIP twin finds the device it runs on.

``vmaf_hip_context_new(&ctx, device_index)`` selects ``device_index``
(``test_hip_device_selection`` checks that without a device). That only
honours the caller's ``--hip_device`` / ``VmafHipConfiguration.device_index``
if every twin passes the device of the context's imported ``VmafHipState``,
which libvmaf hands each extractor as ``fex->hip_device_index``. Before, every
twin passed a literal 0 that ``vmaf_hip_context_new()`` ignored, and a twin ran
on whatever device its thread happened to have.

The contract:

- no ``vmaf_hip_context_new()`` call under ``core/src/feature/hip/`` passes a
  number; the index comes from ``fex->hip_device_index``, directly or through
  a ``device_index`` parameter or field filled from it;
- ``libvmaf.c`` fills ``hip_device_index`` from ``vmaf_hip_state_device_index()``
  for every extractor context it creates;
- ``libvmaf.c`` rebinds the state's device before a frame's twins run and
  before the flush (``vmaf_hip_state_bind()``), so a caller that moves
  ``vmaf_read_pictures()`` to another thread keeps its device.

Device-free: reads the sources only, with a planted regression per check.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HIP_FEATURE_ROOT = ROOT / "core" / "src" / "feature" / "hip"
LIBVMAF = ROOT / "core" / "src" / "libvmaf.c"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
CONTEXT_NEW = re.compile(r"\bvmaf_hip_context_new\s*\(\s*([^,()]+?)\s*,\s*([^()]+?)\s*\)")
FRAMEWORK_INDEX = "fex->hip_device_index"
# A helper parameter that carries the index: its caller in the same file
# passes the framework's field as the last argument.
PARAM_INDEX = "device_index"
PASSES_FRAMEWORK_INDEX = re.compile(r",\s*fex->hip_device_index\s*\)")
# The speed pipeline takes it in its configuration: every extractor that
# creates one fills the field from the framework.
CONFIG_INDEX = "config->device_index"
PIPELINE_CREATE = "speed_hip_pipeline_create("
CONFIG_FILL = "config.device_index = fex->hip_device_index;"
BINDER = "fex_ctx->fex->hip_device_index ="
STATE_INDEX = "vmaf_hip_state_device_index(vmaf->hip.state)"
STATE_BIND = "vmaf_hip_state_bind(vmaf->hip.state)"
BIND_SITES = ("static int read_pictures_hip_frame_begin(", "static int flush_context(")


def _code(source: str) -> str:
    """The source with its comments blanked, so prose cannot trip a check."""
    return COMMENT.sub(" ", source)


def _feature_sources() -> dict[str, str]:
    return {p.name: p.read_text(encoding="utf-8") for p in sorted(HIP_FEATURE_ROOT.glob("*.c"))}


def _index_failure(name: str, code: str, index: str) -> str | None:
    """Why the index a context is created on is not the framework's, or None."""
    if index == FRAMEWORK_INDEX:
        return None
    if index == PARAM_INDEX:
        if PASSES_FRAMEWORK_INDEX.search(code):
            return None
        return f"{name}: '{index}' never comes from fex->hip_device_index in this file"
    if index == CONFIG_INDEX:
        return None  # checked per pipeline user in _pipeline_failures()
    return (
        f"{name}: vmaf_hip_context_new() is given '{index}', "
        "not the framework's fex->hip_device_index"
    )


def _pipeline_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    for name, source in sources.items():
        code = _code(source)
        if PIPELINE_CREATE in code and "int speed_hip_pipeline_create(" not in code:
            if CONFIG_FILL not in code:
                failures.append(f"{name}: the speed pipeline's device_index is not the framework's")
    return failures


def _context_failures(sources: dict[str, str]) -> list[str]:
    failures: list[str] = []
    calls = 0
    for name, source in sources.items():
        code = _code(source)
        for match in CONTEXT_NEW.finditer(code):
            calls += 1
            failure = _index_failure(name, code, re.sub(r"\s+", "", match.group(2)))
            if failure:
                failures.append(failure)
    if calls == 0:
        failures.append("no vmaf_hip_context_new() call found under core/src/feature/hip/")
    return failures + _pipeline_failures(sources)


def _function_body(code: str, signature: str) -> str:
    """The text of the function that starts with ``signature``, up to its end."""
    start = code.find(signature)
    if start < 0:
        return ""
    end = code.find("\n}\n", start)
    return code[start:end] if end > 0 else code[start:]


def _libvmaf_failures(source: str) -> list[str]:
    failures: list[str] = []
    code = _code(source)
    binder = code.find(BINDER)
    if binder < 0 or STATE_INDEX not in code[binder : binder + 200]:
        failures.append(
            "libvmaf.c: no extractor context gets hip_device_index from the imported state"
        )
    if "set_fex_hip_device(fex_ctx, vmaf);" not in code:
        failures.append("libvmaf.c: fex_ctx_bind_backends() no longer sets hip_device_index")
    for signature in BIND_SITES:
        if STATE_BIND not in _function_body(code, signature):
            failures.append(f"libvmaf.c: {signature.split('(')[0][11:]} no longer binds the device")
    return failures


def _contract_failures(features: dict[str, str], libvmaf: str) -> list[str]:
    return _context_failures(features) + _libvmaf_failures(libvmaf)


class HipDeviceIndexContract(unittest.TestCase):
    def _sources(self) -> tuple[dict[str, str], str]:
        return _feature_sources(), LIBVMAF.read_text(encoding="utf-8")

    def assert_detected(self, features: dict[str, str], libvmaf: str, needle: str) -> None:
        failures = _contract_failures(features, libvmaf)
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        features, libvmaf = self._sources()
        self.assertEqual(_contract_failures(features, libvmaf), [])

    def test_literal_device_is_detected(self) -> None:
        # The pre-fix spelling of every twin.
        features, libvmaf = self._sources()
        name = "integer_psnr_hip.c"
        call = "vmaf_hip_context_new(&s->ctx, fex->hip_device_index)"
        self.assertIn(call, features[name])
        features[name] = features[name].replace(call, "vmaf_hip_context_new(&s->ctx, 0)", 1)
        self.assert_detected(features, libvmaf, "integer_psnr_hip.c: vmaf_hip_context_new()")

    def test_helper_index_not_from_the_framework_is_detected(self) -> None:
        features, libvmaf = self._sources()
        name = "integer_cambi_hip.c"
        call = "cambi_hip_setup_device(s, fex->hip_device_index)"
        self.assertIn(call, features[name])
        features[name] = features[name].replace(call, "cambi_hip_setup_device(s, 0)", 1)
        self.assert_detected(features, libvmaf, "never comes from fex->hip_device_index")

    def test_unfilled_pipeline_index_is_detected(self) -> None:
        features, libvmaf = self._sources()
        name = "speed_temporal_hip.c"
        self.assertIn(CONFIG_FILL, features[name])
        features[name] = features[name].replace(CONFIG_FILL, "", 1)
        self.assert_detected(features, libvmaf, "speed_temporal_hip.c: the speed pipeline")

    def test_missing_binder_is_detected(self) -> None:
        features, libvmaf = self._sources()
        self.assertIn(STATE_INDEX, libvmaf)
        libvmaf = libvmaf.replace(STATE_INDEX, "0", 1)
        self.assert_detected(features, libvmaf, "from the imported state")

    def test_missing_frame_bind_is_detected(self) -> None:
        features, libvmaf = self._sources()
        body = _function_body(libvmaf, BIND_SITES[0])
        self.assertIn(STATE_BIND, body)
        libvmaf = libvmaf.replace(body, body.replace(STATE_BIND, "0", 1), 1)
        self.assert_detected(features, libvmaf, "read_pictures_hip_frame_begin no longer binds")

    def test_missing_flush_bind_is_detected(self) -> None:
        features, libvmaf = self._sources()
        body = _function_body(libvmaf, BIND_SITES[1])
        self.assertIn(STATE_BIND, body)
        libvmaf = libvmaf.replace(body, body.replace(STATE_BIND, "0", 1), 1)
        self.assert_detected(features, libvmaf, "flush_context no longer binds")


if __name__ == "__main__":
    unittest.main()
