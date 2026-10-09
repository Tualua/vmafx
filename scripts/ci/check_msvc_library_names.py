#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Check the library files a static MSVC-like build installed (Q-299).

A static MSVC, clang-cl or icx-cl build installs `lib/vmaf.lib` and
`lib/vmafx.lib`, the files the MSVC linker opens for `-lvmaf` / `-lvmafx`, and
not Meson's classic `libvmaf.a` / `libvmafx.a`. The pkg-config files name them
the same way: `libvmaf.pc` has `-lvmaf` and requires `libvmafx`, `libvmafx.pc`
has `-lvmafx`. Netflix/vmaf 3b4dd350e; core/src/meson.build
(`vmaf_static_name_kwargs`).

Usage: check_msvc_library_names.py --prefix DIR [--libdir lib]
Exit 0 when everything is in place, 1 with one line per problem otherwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

LIBS = ("vmaf", "vmafx")


def problems(prefix: Path, libdir: str = "lib") -> list[str]:
    lib = prefix / libdir
    found: list[str] = []
    for name in LIBS:
        if not (lib / f"{name}.lib").is_file():
            found.append(f"{lib / (name + '.lib')}: missing")
        classic = lib / f"lib{name}.a"
        if classic.exists():
            found.append(
                f"{classic}: Meson's classic name, which -l{name} does not open with link.exe"
            )
    pc = lib / "pkgconfig"
    expect = {
        "libvmaf.pc": (r"^Libs:.*\s-lvmaf(\s|$)", r"^Requires:.*\blibvmafx\b"),
        "libvmafx.pc": (r"^Libs:.*\s-lvmafx(\s|$)",),
    }
    for file, patterns in expect.items():
        path = pc / file
        if not path.is_file():
            found.append(f"{path}: missing")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in patterns:
            if not re.search(pattern, text, re.M):
                found.append(f"{path}: no line matching {pattern!r}")
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--libdir", default="lib")
    args = parser.parse_args(argv)
    found = problems(args.prefix, args.libdir)
    for line in found:
        print(f"check_msvc_library_names: {line}")
    if not found:
        print(
            f"check_msvc_library_names: {args.prefix / args.libdir}: vmaf.lib, vmafx.lib and pkg-config files in place"
        )
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
