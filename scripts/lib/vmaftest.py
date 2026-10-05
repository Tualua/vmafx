# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The vmaf CLI a Python test runs: the build under test, never a host install.

A test that ran whatever ``vmaf`` the host had on ``PATH`` or under
``/usr/local/bin`` passed against a stale binary (an older release with
another default model) while the tree under test was never exercised.
:func:`find` reads exactly these sources, in this order:

1. ``VMAF_BIN``;
2. ``VMAF_BIN_FOR_TESTS``;
3. the repository's build directories, ``<dir>/tools/vmaf`` for each of
   :data:`BUILD_DIRS`: ``build`` (the getting-started guide), ``core/build``
   (``make build``) and ``core/build-cpu`` (the Go tests and the ``ai/``
   tools).

A variable that is set is taken as given: when it does not name an
executable file, :func:`find` raises :class:`InvalidVmafBinary` instead of
trying the next source. A relative value is read from the working directory
the test process started in, so a suite that changes directory per test
still finds it. With no variable set and no build, :func:`find` returns
``None`` and the caller skips or fails with :data:`MISSING_MESSAGE`; the
message matches the ``fail_on_skip`` patterns of ``.github/test-suites.json``,
so the suites that need a binary fail on it in CI. There is no fourth source.
The Go tests use the same rule through ``internal/vmaftest``.

Standard library only: every suite imports it as ``scripts.lib.vmaftest``
with the repository root on ``sys.path``, without installing a package.
"""

from __future__ import annotations

import os
from collections.abc import Mapping
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ENV_VARS = ("VMAF_BIN", "VMAF_BIN_FOR_TESTS")
BUILD_DIRS = ("build", "core/build", "core/build-cpu")
BUILD_COMMAND = "meson setup build core && ninja -C build"
MISSING_MESSAGE = (
    "vmaf binary under test not found: set VMAF_BIN (or VMAF_BIN_FOR_TESTS) to the vmaf "
    f"CLI of the build under test, or build one ({BUILD_COMMAND}); "
    "PATH and /usr/local/bin are never searched"
)

# Relative variable values are read from here, not from a per-test directory.
_START_DIR = Path.cwd()


class InvalidVmafBinary(Exception):
    """A variable names a vmaf binary that is not an executable file."""


def executable_name() -> str:
    """The CLI's file name on this platform."""
    return "vmaf.exe" if os.name == "nt" else "vmaf"


def _is_executable(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def _from_variable(name: str, value: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = _START_DIR / path
    if not _is_executable(path):
        raise InvalidVmafBinary(
            f"{name}={value} does not name an executable file ({path}): point it at the "
            f"vmaf CLI of the build under test or unset it"
        )
    return path


def find(environ: Mapping[str, str] | None = None, root: Path = REPO_ROOT) -> Path | None:
    """The vmaf CLI under test, or ``None`` when neither variable is set and no build exists.

    ``environ`` defaults to ``os.environ`` and ``root`` to this repository;
    both are parameters so the order can be tested without touching either.
    """
    env = os.environ if environ is None else environ
    for name in ENV_VARS:
        value = env.get(name, "")
        if value:
            return _from_variable(name, value)
    for build_dir in BUILD_DIRS:
        candidate = root / build_dir / "tools" / executable_name()
        if _is_executable(candidate):
            return candidate
    return None
