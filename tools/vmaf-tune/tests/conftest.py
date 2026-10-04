# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Suite-wide pytest setup for the vmaf-tune tests."""

from __future__ import annotations

from pathlib import Path

import pytest


@pytest.fixture(autouse=True)
def _private_working_directory(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Run every test in its own temporary working directory.

    Subprocess fakes write to the last argv element; for the encoder-version
    probe (``ffmpeg -version``) that left a ``-version`` file in the checkout,
    and relative defaults such as ``.workingdir/cache/vmafx-tune/encodes``
    landed there too. Tests reach repository files through ``__file__``.
    """
    monkeypatch.chdir(tmp_path)
