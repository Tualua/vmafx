#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Pin vif_cuda's logarithm to the expression of the CPU's table (ADR-1456).

The CPU ``vif`` reads its logarithms from a table that
``vif_log2_table_generate()`` fills with the host math library:
``roundf(log2f(32768 + i) * 2048)``. ``vif_cuda`` evaluates the same
expression per pixel on the device (``log_generate()``), and
``test_cuda_vif_log2_table`` compares the device's value with the host table
for every entry through the probe kernel ``vif_log2_table_probe``.

That device test proves the twin only while three things hold, which this
test reads from the sources:

- the statistic takes every logarithm from ``log_generate()``;
- ``log_generate()`` is the table's expression (``roundf``, not a
  round-to-even conversion: 80 entries are ties);
- the probe kernel calls ``log_generate()`` of the statistic's header and is
  built by the fatbin rule the statistic's module is built by, so it measures
  the value the statistic adds.

Device-free.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FEATURE = ROOT / "core" / "src" / "feature"

STATISTIC = "cuda/integer_vif/vif_statistics.cuh"
KERNELS = "cuda/integer_vif/filter1d.cu"
PROBE = "cuda/integer_vif/vif_log2_probe.cu"
BUILD = ROOT / "core" / "src" / "meson.build"
TABLE = "vif_log2_table.h"
DEVICE_TEST = ROOT / "core" / "test" / "test_cuda_vif_log2_table.c"

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

LOG_GENERATE = "return (uint16_t)roundf(log2f(float(i)) * 2048.f);"
TABLE_EXPRESSION = (
    "log2_table[i] = (uint16_t)roundf(log2f((float)(VIF_LOG2_TABLE_OFFSET + i)) * 2048);"
)
STATISTIC_SITES = (
    "den_val = log_generate(log_den1);",
    "num_val = log_generate(numlog) - log_generate(denlog);",
)
PROBE_BODY = "reinterpret_cast<uint16_t *>(table.data)[i] = log_generate((int)(offset + i));"
PROBE_INCLUDE = '#include "vif_statistics.cuh"'
FATBIN_ENTRIES = (
    "'filter1d' : [feature_src_dir + 'cuda/integer_vif/filter1d.cu'],",
    "'vif_log2_probe' : [feature_src_dir + 'cuda/integer_vif/vif_log2_probe.cu'],",
)
PROBE_ARGUMENTS = (
    "unsigned offset = VIF_LOG2_TABLE_OFFSET;",
    "unsigned count = VIF_LOG2_TABLE_SIZE;",
    "for (unsigned i = 0; i < VIF_LOG2_TABLE_SIZE; i++) {",
    "vif_log2_table_generate(host);",
)
# A logarithm evaluated anywhere but in log_generate().
OTHER_LOG = re.compile(r"\blog(?:2|10|1p)?f?\s*\(")


def _flat(source: str) -> str:
    """Code without comments, every run of whitespace collapsed."""
    return " ".join(COMMENT.sub(" ", source).split())


def _sources() -> dict[str, str]:
    sources = {
        name: (FEATURE / name).read_text(encoding="utf-8")
        for name in (STATISTIC, KERNELS, PROBE, TABLE)
    }
    sources["test"] = DEVICE_TEST.read_text(encoding="utf-8")
    sources["build"] = BUILD.read_text(encoding="utf-8")
    return sources


def _function_body(code: str, signature: str) -> str:
    """Flattened text of the definition that starts with `signature` (brace-matched), or empty."""
    start = code.find(signature)
    if start < 0:
        return ""
    depth = 0
    for index in range(code.index("{", start), len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[start : index + 1]
    return ""


def _statistic_failures(statistic: str, kernels: str) -> list[str]:
    code = _flat(statistic)
    failures: list[str] = []
    generate = _function_body(code, "uint16_t log_generate(int i)")
    if LOG_GENERATE not in generate:
        failures.append(f"{STATISTIC}: log_generate() is not the table's expression")
    rest = code.replace(generate, " ") if generate else code
    if any(site not in rest for site in STATISTIC_SITES):
        failures.append(f"{STATISTIC}: the statistic does not take each logarithm from log_generate()")
    if OTHER_LOG.search(rest) or OTHER_LOG.search(_flat(kernels)):
        failures.append(f"{STATISTIC}: a logarithm is evaluated outside log_generate()")
    return failures


def _probe_failures(probe: str, test: str, build: str) -> list[str]:
    failures: list[str] = []
    code = _flat(probe)
    if PROBE_BODY not in _function_body(code, "void vif_log2_table_probe("):
        failures.append(f"{PROBE}: the probe kernel does not write log_generate()")
    if PROBE_INCLUDE not in code or OTHER_LOG.search(code):
        failures.append(f"{PROBE}: the probe does not take log_generate() from the statistic's header")
    if any(piece not in _flat(test) for piece in PROBE_ARGUMENTS):
        failures.append("the device test does not compare the whole table with the host's")
    # Both modules go through the one fatbin rule, so they take the same flags.
    if any(entry not in build for entry in FATBIN_ENTRIES) or "cuda_cu_extra_flags = {}" not in build:
        failures.append("the probe and the statistic are not built by the same rule and flags")
    return failures


def _table_failures(table: str) -> list[str]:
    if TABLE_EXPRESSION not in _flat(table):
        return [f"{TABLE}: the table's expression changed; log_generate() mirrors it"]
    return []


def _contract_failures(sources: dict[str, str]) -> list[str]:
    return (
        _statistic_failures(sources[STATISTIC], sources[KERNELS])
        + _probe_failures(sources[PROBE], sources["test"], sources["build"])
        + _table_failures(sources[TABLE])
    )


class CudaVifLog2Contract(unittest.TestCase):
    def _edited(self, name: str, old: str, new: str) -> list[str]:
        sources = _sources()
        self.assertIn(old, sources[name])
        sources[name] = sources[name].replace(old, new, 1)
        return _contract_failures(sources)

    def _assert_detected(self, failures: list[str], needle: str) -> None:
        self.assertTrue(any(needle in item for item in failures), failures)

    def test_sources_satisfy_the_contract(self) -> None:
        self.assertEqual(_contract_failures(_sources()), [])

    def test_round_to_even_is_detected(self) -> None:
        # What vif_hip had: 80 ties round the other way.
        failures = self._edited(
            STATISTIC,
            "return (uint16_t)roundf(log2f(float(i)) * 2048.f);",
            "return (uint16_t)__float2int_rn(log2f(float(i)) * 2048.f);",
        )
        self._assert_detected(failures, "not the table's expression")

    def test_inline_logarithm_in_the_statistic_is_detected(self) -> None:
        # A logarithm the probe does not measure.
        failures = self._edited(
            STATISTIC,
            "den_val = log_generate(log_den1);",
            "den_val = (uint16_t)roundf(log2f((float)log_den1) * 2048.f);",
        )
        self._assert_detected(failures, "does not take each logarithm from log_generate()")
        self._assert_detected(failures, "outside log_generate()")

    def test_probe_with_its_own_expression_is_detected(self) -> None:
        failures = self._edited(
            PROBE,
            "log_generate((int)(offset + i));",
            "(uint16_t)roundf(log2f((float)(offset + i)) * 2048.f);",
        )
        self._assert_detected(failures, "probe kernel does not write log_generate()")
        self._assert_detected(failures, "from the statistic's header")

    def test_probe_built_with_its_own_flags_is_detected(self) -> None:
        failures = self._edited(
            "build",
            "cuda_cu_extra_flags = {}",
            "cuda_cu_extra_flags = {'vif_log2_probe' : ['--use_fast_math']}",
        )
        self._assert_detected(failures, "same rule and flags")

    def test_partial_probe_is_detected(self) -> None:
        failures = self._edited(
            "test",
            "    unsigned count = VIF_LOG2_TABLE_SIZE;",
            "    unsigned count = VIF_LOG2_TABLE_SIZE / 2u;",
        )
        self._assert_detected(failures, "whole table")

    def test_changed_table_expression_is_detected(self) -> None:
        # An upstream change to the table changes log_generate() with it.
        failures = self._edited(
            TABLE,
            "(uint16_t)roundf(log2f((float)(VIF_LOG2_TABLE_OFFSET + i)) * 2048);",
            "(uint16_t)round(log2((double)(VIF_LOG2_TABLE_OFFSET + i)) * 2048);",
        )
        self._assert_detected(failures, "log_generate() mirrors it")


if __name__ == "__main__":
    unittest.main()
