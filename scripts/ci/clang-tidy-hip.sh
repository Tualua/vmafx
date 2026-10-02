#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# clang-tidy for the hip lane (ADR-1471): the lane's clang-tidy for the host
# translation units, the ROCm toolchain's own clang-tidy for the .hip kernels.
#
# A .hip kernel includes ROCm's device headers. Those call builtins that only
# the LLVM shipped with ROCm knows (ROCm 10: `__builtin_amdgcn_is_invocable`
# in hip/amd_detail/amd_device_functions.h), so a stock clang-tidy stops at
# "builtin functions must be directly called" before it reaches the kernel's
# own code. hipcc is that LLVM's clang; its clang-tidy sits next to it.
#
# Usage (mirrors clang-tidy):
#   scripts/ci/clang-tidy-hip.sh -p <build-dir> [other args] <file>
#
# Environment:
#   CLANG_TIDY_BIN      clang-tidy for everything that is not a .hip file
#                       (default: `clang-tidy` from PATH). `--version` reports
#                       this one, so it is the version a baseline records.
#   HIP_CLANG_TIDY_BIN  clang-tidy for .hip files (default:
#                       ${ROCM_PATH:-/opt/rocm}/llvm/bin/clang-tidy).
#
# Exit code = the chosen clang-tidy's exit code; 127 when it is missing.
set -euo pipefail

CLANG_TIDY_BIN="${CLANG_TIDY_BIN:-clang-tidy}"
HIP_CLANG_TIDY_BIN="${HIP_CLANG_TIDY_BIN:-${ROCM_PATH:-/opt/rocm}/llvm/bin/clang-tidy}"

binary="$CLANG_TIDY_BIN"
for arg in "$@"; do
  case "$arg" in
    -*) ;;
    *.hip) binary="$HIP_CLANG_TIDY_BIN" ;;
  esac
done

if ! command -v "$binary" >/dev/null 2>&1; then
  # An `error:` line: tidy-ratchet.py counts the translation unit as unusable
  # instead of as clean.
  echo "error: clang-tidy binary '$binary' not found" >&2
  exit 127
fi
exec "$binary" "$@"
