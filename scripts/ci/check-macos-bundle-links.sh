#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# Fail unless every Mach-O file of the macOS tester bundle links only system
# libraries or files inside the bundle.
#   - bin/vmaf and the test executables (build/, tests/): /usr/lib and /System/Library
#     only; they are self-contained.
#   - everything else (the bundled interpreter and its extension modules): the same, plus
#     references that resolve to an existing file INSIDE the bundle.
# `otool -L` lists a dylib's own install name first (LC_ID_DYLIB, shown by `otool -D`):
# it is skipped. `@rpath/x` is resolved against the file's own LC_RPATH entries
# (`otool -l`), `@loader_path/x` against the file's directory and `@executable_path/x`
# against the file's directory and runtime/bin; a bare `x` against the file's directory.
# A reference that resolves outside the bundle (Homebrew, runner paths, `..` escapes)
# or not at all fails.
# Usage: check-macos-bundle-links.sh <bundle-dir>
# Needs `otool` (Xcode command line tools; present on the hosted macOS runner).
# Runs under bash 3.2 (macOS).
set -euo pipefail

bundle=${1:?usage: check-macos-bundle-links.sh <bundle-dir>}
bundle=$(cd "$bundle" && pwd -P)
fail=0

is_macho() { file -b "$1" | grep -q 'Mach-O'; }

allowed_system() {
  case "$1" in
    /usr/lib/* | /System/Library/*) return 0 ;;
    *) return 1 ;;
  esac
}

# Print the physical path of an existing file, or fail.
physical() {
  local dir
  dir=$(cd "$(dirname "$1")" 2>/dev/null && pwd -P) || return 1
  [ -e "$dir/$(basename "$1")" ] || return 1
  printf '%s/%s\n' "$dir" "$(basename "$1")"
}

inside_bundle() {
  case "$1" in
    "$bundle"/*) return 0 ;;
    *) return 1 ;;
  esac
}

# expand_loader <file> <path>: replace @loader_path / @executable_path prefixes.
expand_loader() {
  local file=$1 path=$2 dir
  dir=$(dirname "$file")
  case "$path" in
    @loader_path/*) printf '%s/%s\n' "$dir" "${path#@loader_path/}" ;;
    @executable_path/*) printf '%s/%s\n' "$dir" "${path#@executable_path/}" ;;
    *) printf '%s\n' "$path" ;;
  esac
}

# candidates <file> <dependency>: every path the reference may resolve to, one per line.
candidates() {
  local file=$1 dep=$2 rpath
  case "$dep" in
    @rpath/*)
      while IFS= read -r rpath; do
        [ -n "$rpath" ] || continue
        printf '%s/%s\n' "$(expand_loader "$file" "$rpath")" "${dep#@rpath/}"
      done < <(otool -l "$file" | awk '/cmd LC_RPATH/ {f = 1} f && $1 == "path" {print $2; f = 0}')
      ;;
    @loader_path/*) expand_loader "$file" "$dep" ;;
    @executable_path/*)
      expand_loader "$file" "$dep"
      printf '%s/runtime/bin/%s\n' "$bundle" "${dep#@executable_path/}"
      ;;
    /*) ;;
    *) printf '%s/%s\n' "$(dirname "$file")" "$dep" ;;
  esac
}

resolves_in_bundle() { # <file> <dependency>
  local candidate real
  while IFS= read -r candidate; do
    real=$(physical "$candidate") || continue
    if inside_bundle "$real"; then return 0; fi
  done < <(candidates "$1" "$2")
  return 1
}

check_file() {
  local file=$1 strict=$2 dep rel id
  rel=${file#"$bundle"/}
  id=$(otool -D "$file" | tail -n +2 | head -n 1 | awk '{print $1}')
  # otool -L prints the file name first, then one dependency per line.
  while read -r dep; do
    if [ -n "$id" ] && [ "$dep" = "$id" ]; then
      continue
    fi
    if allowed_system "$dep"; then
      continue
    fi
    if [ "$strict" = no ] && resolves_in_bundle "$file" "$dep"; then
      continue
    fi
    echo "::error::$rel links $dep (not a system library, and not a file inside the bundle)" >&2
    fail=1
  done < <(otool -L "$file" | tail -n +2 | awk '{print $1}')
}

while IFS= read -r file; do
  is_macho "$file" || continue
  case "$file" in
    "$bundle"/build/* | "$bundle"/tests/*) check_file "$file" yes ;;
    *) check_file "$file" no ;;
  esac
done < <(find "$bundle" -type f)

exit "$fail"
