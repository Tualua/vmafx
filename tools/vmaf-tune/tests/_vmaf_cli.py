# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Shared lookups of the VMAFx C sources and ``vmaf`` CLI for the vmaf-tune tests.

The source-pinning tests read the CLI sources under ``core/tools/`` and
the integration tests run the fork's ``vmaf`` binary. Both lookups live
here so a rename (``vmaf.c`` to ``vmaf.cpp``, ``libvmaf/`` to ``core/``)
fails every caller at once instead of turning some of them into silent
skips.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]


def repo_source(relative: str) -> str:
    """Return the text of ``relative`` under the repository root.

    Skips only when the tests run outside the VMAFx repository (no
    ``core/meson.build``); inside it a missing file is a failure, so a
    renamed source cannot hide behind a skip.
    """
    if not (REPO_ROOT / "core" / "meson.build").is_file():
        pytest.skip("not inside the VMAFx repository: core/meson.build is absent")
    return (REPO_ROOT / relative).read_text(encoding="utf-8")


def binary_supports_backend_flag(path: Path) -> bool:
    """True iff the binary advertises ``--backend`` in its help output.

    A ``vmaf`` that does not is not this fork's CLI (an upstream system
    install such as ``/usr/local/bin/vmaf`` 3.2.0, which even exits 0 on
    the unknown option), so it cannot stand in for the binary under test.
    """
    try:
        result = subprocess.run([str(path), "--help"], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return "--backend" in (result.stdout + result.stderr)


def resolve_vmaf_binary(
    start: Path, supports: Callable[[Path], bool] = binary_supports_backend_flag
) -> Path | None:
    """Locate this fork's ``vmaf`` CLI, or ``None``.

    Order: ``$VMAF_BIN_FOR_TESTS`` (taken as given), the ``vmaf`` on
    ``PATH``, then ``build/tools/vmaf`` / ``core/build/tools/vmaf`` under
    the nearest repository root above ``start``; the last two only when
    ``supports`` accepts them.
    """
    env = os.environ.get("VMAF_BIN_FOR_TESTS")
    if env:
        env_path = Path(env)
        if env_path.is_file() and os.access(env_path, os.X_OK):
            return env_path
    which = shutil.which("vmaf")
    if which and supports(Path(which)):
        return Path(which)
    for parent in [start, *start.parents]:
        if (parent / "meson.build").is_file() or (parent / "core" / "meson.build").is_file():
            for rel in (Path("build/tools/vmaf"), Path("core/build/tools/vmaf")):
                candidate = parent / rel
                if candidate.is_file() and os.access(candidate, os.X_OK) and supports(candidate):
                    return candidate
            break
    return None
