#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Whether a feature extractor names its features before it writes an option slot.

`vmaf_feature_name_dict_from_provided_features()` derives every emitted name
from the options the extractor holds when it is called: an option slot with
VMAF_OPT_FLAG_FEATURE_PARAM and a non-default value adds a suffix. cambi.c and
every twin build that dictionary in init() before anything writes an option
slot, so the names follow the options as the caller set them. A slot written
first changes the names: integer_cambi_metal's resolved encode and source sizes
gave `cambi_encbd_8_ench_324_encw_576_srch_324_srcw_576`
(T-METAL-CAMBI-SCORE-NAME-SUFFIXED-2026-10-05), and integer_vif_cuda's
cleared `enable_chroma` dropped the suffix the caller asked for
(T-GPU-VIF-NAMES-AFTER-OPTION-RESET-2026-10-05).

`init_order_failures()` walks, for each extractor a source registers, the code
its init() runs before the dictionary: init's text up to the call that leads
there, that function's text up to its next step, and so on, plus every
function of the same source those texts call. An option slot is a field named
by an `offsetof()` of the source. Static text only; device-free.
"""

from __future__ import annotations

import functools
import re

import metal_option_tables as tables

NAME_DICT = "vmaf_feature_name_dict_from_provided_features("
# A source that holds either has names that can depend on its options.
NAMED_BY_OPTIONS = ("VMAF_OPT_FLAG_FEATURE_PARAM", "feature_name_dict")
REGISTRATION = re.compile(r"VmafFeatureExtractor\s+vmaf_fex_\w+\s*=\s*\{")
INIT = re.compile(r"\.init\s*=\s*(\w+)")
CALLED = re.compile(r"\b(\w+)\s*\(")
DEFINITION = re.compile(r"\b(\w+)\s*\((?:[^;{}()]|\([^()]*\))*\)\s*\{")
KEYWORDS = frozenset({"if", "for", "while", "switch", "return", "sizeof", "catch"})
SLOT_WRITE = re.compile(r"\b\w+\s*->\s*(\w+)\s*=(?!=)")
OPTION_SLOT = re.compile(r"offsetof\(\s*\w+\s*,\s*(\w+)\s*\)")
# How deep the walk follows calls, and how many functions it reads at most.
CALL_DEPTH = 6
MAX_VISITS = 256


@functools.lru_cache(maxsize=256)
def functions(code: str) -> dict[str, str]:
    """Name -> body of every function `code` defines (first definition wins)."""
    found: dict[str, str] = {}
    for match in DEFINITION.finditer(code):
        name = match.group(1)
        if name in KEYWORDS or name in found:
            continue
        found[name] = code[match.end() : tables.balanced_end(code, match.end() - 1)]
    return found


def naming(code: str) -> set[str]:
    """The functions of `code` that build the dictionary, directly or through a
    function of `code` they call (at most CALL_DEPTH calls deep)."""
    bodies = functions(code)
    named = {name for name, body in bodies.items() if NAME_DICT in body}
    for _ in range(CALL_DEPTH):
        grown = {name for name, body in bodies.items() if set(CALLED.findall(body)) & named}
        if grown <= named:
            break
        named |= grown
    return named


def before_names(code: str, init: str) -> list[str] | None:
    """The texts init runs before the dictionary; None when it never builds it."""
    named = naming(code)
    texts: list[str] = []
    name = init
    for _ in range(CALL_DEPTH):
        body = functions(code).get(name)
        if body is None:
            return None
        at = body.find(NAME_DICT)
        if at >= 0:
            return [*texts, body[:at]]
        step = next((m for m in CALLED.finditer(body) if m.group(1) in named), None)
        if step is None:
            return None
        texts.append(body[: step.start()])
        name = step.group(1)
    return None


def slot_writes(code: str, texts: list[str]) -> set[str]:
    """Option slots written in `texts` or in the functions of `code` they call."""
    slots = set(OPTION_SLOT.findall(code))
    work = [(text, 0) for text in texts]
    seen: set[str] = set()
    found: set[str] = set()
    for _ in range(MAX_VISITS):
        if not work:
            break
        text, depth = work.pop()
        found |= {field for field in SLOT_WRITE.findall(text) if field in slots}
        if depth >= CALL_DEPTH:
            continue
        for callee in sorted(set(CALLED.findall(text)) - seen):
            seen.add(callee)
            body = functions(code).get(callee)
            if body is not None:
                work.append((body, depth + 1))
    return found


def init_order_failures(label: str, source: str) -> list[str]:
    """One message per extractor of `source` whose init writes an option slot
    before it names its features, or that never names them although its names
    can depend on its options (a FEATURE_PARAM option, or a feature-name
    dictionary it emits through; the option table may come from a shared
    header). An extractor with neither emits fixed names and may skip the
    dictionary (the ssimulacra2 twins)."""
    code = tables.strip_comments(source)
    failures = []
    for registration in REGISTRATION.finditer(code):
        end = tables.balanced_end(code, registration.end() - 1)
        init = INIT.search(code[registration.end() : end])
        if not init:
            continue
        texts = before_names(code, init.group(1))
        if texts is None:
            if any(marker in code for marker in NAMED_BY_OPTIONS):
                failures.append(f"{label}: {init.group(1)} builds no feature-name dictionary")
            continue
        writes = slot_writes(code, texts)
        if writes:
            failures.append(
                f"{label}: {init.group(1)} writes option slot(s) {sorted(writes)} before it "
                "builds the feature-name dictionary"
            )
    return failures
