# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The C compiler command a Meson test hands to a Python test.

core/test/meson.build passes `cc.cmd_array()` one word per argument as
`--cc=<word>`. Meson puts a compiler launcher first when it finds one
(`ccache cc ...`), so the first word alone is not a compiler: running it with
compiler flags fails (`ccache: invalid option -- 'a'`).
"""

from __future__ import annotations

PREFIX = "--cc="


def take_cc(argv: list[str]) -> list[str]:
    """Remove the leading `--cc=<word>` arguments from argv[1:] and return the words."""
    words: list[str] = []
    while len(argv) > 1 and argv[1].startswith(PREFIX):
        words.append(argv.pop(1)[len(PREFIX) :])
    if not words:
        raise SystemExit(f"usage: {argv[0]} --cc=<word>... (the compiler command, one word each)")
    return words
