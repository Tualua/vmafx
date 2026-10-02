#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# golden-arm64-preflight.sh — what `make test-netflix-golden-arm64` needs
# before it configures anything (ADR-1461).
#
# The aarch64 golden gate cross-builds the `vmaf` tool and lets the Python
# harness execute it like a native program: the kernel's binfmt_misc handler
# hands an aarch64 ELF to qemu-aarch64. Three things can be missing on an
# x86 host, and each one fails late and unreadably if it is not checked here:
# the cross compiler (Meson: "Unknown compiler(s)"), the aarch64 C library the
# binary is loaded against (qemu: "Could not open '/lib/ld-linux-aarch64.so.1'"),
# and the binfmt handler (the harness: "Exec format error" on every test).
#
# Usage:
#   golden-arm64-preflight.sh <gcc|clang> <cross-file> <sysroot>
#
# Environment (tests point these at fixtures):
#   GOLDEN_ARM64_BINFMT_ENTRY  binfmt_misc entry to read
#                              (default /proc/sys/fs/binfmt_misc/qemu-aarch64)
#
# Exit 0 when the gate can run, 1 with one line per missing piece, 2 on a
# usage error.

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <gcc|clang> <cross-file> <sysroot>" >&2
  exit 2
fi

compiler="$1"
cross_file="$2"
sysroot="$3"
binfmt_entry="${GOLDEN_ARM64_BINFMT_ENTRY:-/proc/sys/fs/binfmt_misc/qemu-aarch64}"
missing=0

fail() {
  echo "error: $1" >&2
  echo "       $2" >&2
  missing=1
}

case "${compiler}" in
  gcc)
    tools=(aarch64-linux-gnu-gcc aarch64-linux-gnu-g++)
    hint="install the aarch64 cross GCC (Arch: aarch64-linux-gnu-gcc; Debian/Ubuntu: gcc-aarch64-linux-gnu g++-aarch64-linux-gnu)"
    ;;
  clang)
    tools=(clang clang++ ld.lld aarch64-linux-gnu-ar)
    hint="install clang, lld and the aarch64 cross binutils and GCC (clang uses their C library and libstdc++)"
    ;;
  *)
    echo "error: GOLDEN_ARM64_CC must be gcc or clang, got '${compiler}'" >&2
    exit 2
    ;;
esac

for tool in "${tools[@]}"; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    fail "cross compiler: '${tool}' not found" "${hint}"
  fi
done

if [[ ! -f "${cross_file}" ]]; then
  fail "cross file '${cross_file}' not found" \
    "set GOLDEN_ARM64_CROSS_FILE to a Meson cross file for aarch64 (see build-aux/)"
fi

if [[ ! -e "${sysroot}/lib/ld-linux-aarch64.so.1" ]]; then
  fail "aarch64 sysroot: no lib/ld-linux-aarch64.so.1 under '${sysroot}'" \
    "install the aarch64 C library (Arch: aarch64-linux-gnu-glibc; Debian/Ubuntu: libc6-arm64-cross) or set QEMU_LD_PREFIX"
fi

if [[ ! -r "${binfmt_entry}" ]]; then
  fail "binfmt handler: '${binfmt_entry}' does not exist" \
    "register qemu-aarch64 with binfmt_misc (Arch: qemu-user-static qemu-user-static-binfmt; Debian/Ubuntu: qemu-user-static binfmt-support)"
elif ! head -n 1 "${binfmt_entry}" | grep -qx 'enabled'; then
  fail "binfmt handler: '${binfmt_entry}' is not enabled" \
    "enable it: echo 1 | sudo tee ${binfmt_entry}"
fi

if [[ "${missing}" -ne 0 ]]; then
  echo "       The aarch64 golden gate cannot run on this host; nothing was built." >&2
  exit 1
fi
