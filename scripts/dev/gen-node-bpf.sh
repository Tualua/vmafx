#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# gen-node-bpf.sh -- generate the vmafx-node eBPF object and its Go binding.
#
# cmd/vmafx-node/bpf/rclone_bypass.bpf.c is compiled with clang through bpf2go
# into rclonebypass_bpfel.o (embedded in the node binary) and
# rclonebypass_bpfel.go. The object is not committed (ADR-1622): every place the
# node is built runs this script first. The binding is source and is committed;
# the script regenerates it and, with --require-pin, fails when it changed. This
# is the one implementation of that step; the Makefile, the Go CI job,
# docker/Dockerfile.node and dev/Containerfile call it, and `go generate
# ./cmd/vmafx-node/bpf/` stays the underlying command.
#
# Usage:
#   scripts/dev/gen-node-bpf.sh [--require-pin] [--force]
#
#   --require-pin  fail unless clang is exactly BPF_CLANG_VERSION (build-config.env)
#                  and the object's sha256 is BPF_OBJECT_SHA256. CI, the container
#                  build and the release path pass it; a contributor build without
#                  it names the substitution and still builds.
#   --force        regenerate even when the outputs match the inputs' stamp.
#
# Environment:
#   BPF_CLANG       clang to use (default: clang-<major of the pin>, then clang)
#   BPF_LLVM_STRIP  llvm-strip to use (default: llvm-strip-<major>, then llvm-strip)
#
# Exit status: 0 generated (or up to date), 2 a tool is missing or has the
# wrong version, 3 the object's digest differs from the pin, 4 (--require-pin) the
# committed rclonebypass_bpfel.go differs from the regenerated one, other: failure.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pkg="${root}/cmd/vmafx-node/bpf"
config="${root}/build-config.env"
require_pin=0
force=0
for arg in "$@"; do
  case "${arg}" in
    --require-pin) require_pin=1 ;;
    --force) force=1 ;;
    *)
      echo "gen-node-bpf: unknown argument '${arg}'" >&2
      exit 64
      ;;
  esac
done

# Reads KEY="value" from build-config.env without executing the file.
pin() {
  sed -n "s/^$1=\"\\([^\"]*\\)\".*/\\1/p" "${config}" | head -n 1
}

want_clang="$(pin BPF_CLANG_VERSION)"
want_sha="$(pin BPF_OBJECT_SHA256)"
if [ -z "${want_clang}" ] || [ -z "${want_sha}" ]; then
  echo "gen-node-bpf: BPF_CLANG_VERSION / BPF_OBJECT_SHA256 missing from ${config}" >&2
  exit 2
fi
major="${want_clang%%.*}"

install_hint="install clang ${want_clang} with the BPF target and libbpf headers
  Debian 13:  apt-get install clang-${major} llvm-${major} libbpf-dev
  Arch Linux: pacman -S clang llvm libbpf
  (see docs/development/node-ebpf-build.md)"

# The versioned name comes first: a host can carry the pinned release as
# `<name>-<major>` beside a different default `<name>` (the GitHub-hosted
# Ubuntu runner ships clang 21 as /usr/bin/clang), and the pin must win.
find_tool() { # find_tool <override> <name> -> prints the first one on PATH
  local candidate
  for candidate in "$1" "$2-${major}" "$2"; do
    if [ -n "${candidate}" ] && command -v "${candidate}" >/dev/null 2>&1; then
      command -v "${candidate}"
      return 0
    fi
  done
  return 1
}

if ! clang="$(find_tool "${BPF_CLANG:-}" clang)"; then
  echo "gen-node-bpf: clang not found. The node's eBPF object is generated at build time" >&2
  echo "  and no pre-built object is committed or downloaded; ${install_hint}" >&2
  exit 2
fi
if ! "${clang}" -print-targets 2>/dev/null | grep -qE '^[[:space:]]+bpf[[:space:]]'; then
  echo "gen-node-bpf: ${clang} has no BPF target (clang -print-targets lists none); ${install_hint}" >&2
  exit 2
fi
if ! strip_tool="$(find_tool "${BPF_LLVM_STRIP:-}" llvm-strip)"; then
  echo "gen-node-bpf: llvm-strip not found (bpf2go strips DWARF with it); ${install_hint}" >&2
  exit 2
fi
if [ ! -f /usr/include/bpf/bpf_helpers.h ]; then
  echo "gen-node-bpf: /usr/include/bpf/bpf_helpers.h not found (libbpf headers); ${install_hint}" >&2
  exit 2
fi
if ! command -v go >/dev/null 2>&1; then
  echo "gen-node-bpf: go not found; install the version declared by go.mod" >&2
  exit 2
fi

have_clang="$("${clang}" --version | sed -n 's/^.*clang version \([0-9][0-9.]*\).*/\1/p' | head -n 1)"
pinned=1
if [ "${have_clang}" != "${want_clang}" ]; then
  pinned=0
  if [ "${require_pin}" -eq 1 ]; then
    echo "gen-node-bpf: ${clang} is clang ${have_clang}, the pinned release is ${want_clang} (BPF_CLANG_VERSION in build-config.env); ${install_hint}" >&2
    exit 2
  fi
  echo "gen-node-bpf: using clang ${have_clang}, not the pinned ${want_clang}: the object works but is not byte-identical to a release build" >&2
fi

# Up to date when the stamp (inputs + tool identity) matches and both outputs exist.
inputs_sha="$(cat "${pkg}/rclone_bypass.bpf.c" "${pkg}/vmlinux.h" "${pkg}/gen.go" "${pkg}/add_spdx_header.sh" "${pkg}/embed_generated_object.sh" "${root}/go.mod" |
  sha256sum | cut -d' ' -f1)"
stamp="${inputs_sha}  ${have_clang}"
stamp_file="${pkg}/.rclonebypass.stamp"
if [ "${force}" -eq 0 ] && [ -f "${pkg}/rclonebypass_bpfel.o" ] && [ -f "${pkg}/rclonebypass_bpfel.go" ] &&
  [ -f "${stamp_file}" ] && [ "$(cat "${stamp_file}")" = "${stamp}" ]; then
  echo "gen-node-bpf: up to date (clang ${have_clang}); sha256 $(sha256sum "${pkg}/rclonebypass_bpfel.o" | cut -d' ' -f1)"
else
  # The object is not committed; the binding is, and the generator rewrites it.
  committed_binding="$(mktemp)"
  trap 'rm -f "${committed_binding}"' EXIT
  cp "${pkg}/rclonebypass_bpfel.go" "${committed_binding}"
  rm -f "${pkg}/rclonebypass_bpfel.o" "${stamp_file}"
  (cd "${root}" && BPF2GO_CC="${clang}" BPF2GO_STRIP="${strip_tool}" go generate ./cmd/vmafx-node/bpf/)
  printf '%s\n' "${stamp}" >"${stamp_file}"
  if ! cmp -s "${committed_binding}" "${pkg}/rclonebypass_bpfel.go"; then
    if [ "${require_pin}" -eq 1 ]; then
      echo "gen-node-bpf: the committed rclonebypass_bpfel.go is stale: regenerating it changed the file." >&2
      echo "  Run scripts/dev/gen-node-bpf.sh locally and commit the regenerated binding with the C source change." >&2
      exit 4
    fi
    echo "gen-node-bpf: rclonebypass_bpfel.go changed: commit the regenerated binding with the C source change" >&2
  fi
fi

got_sha="$(sha256sum "${pkg}/rclonebypass_bpfel.o" | cut -d' ' -f1)"
echo "gen-node-bpf: clang ${have_clang} (pin ${want_clang}); rclonebypass_bpfel.o sha256 ${got_sha}"
if [ "${pinned}" -eq 1 ] && [ "${got_sha}" != "${want_sha}" ]; then
  echo "gen-node-bpf: the object does not match BPF_OBJECT_SHA256 ${want_sha} although the compiler is the pinned one;" >&2
  echo "  a changed source, header, flag or libbpf header changes the object. Re-record the digest in build-config.env if the change is intended." >&2
  exit 3
fi
