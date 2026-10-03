#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# Fail unless every Mach-O file of the macOS tester bundle links only system
# libraries or files inside the bundle.
#   - bin/vmaf and the test executables: /usr/lib and /System/Library only
#     (no Homebrew, no @rpath, no bundle file: they are self-contained).
#   - the bundled interpreter and its extension modules: the same, plus
#     @executable_path / @loader_path references that resolve to a file in the bundle.
# Usage: check-macos-bundle-links.sh <bundle-dir>
# Needs `otool` (Xcode command line tools; present on the hosted macOS runner).
set -euo pipefail

bundle=${1:?usage: check-macos-bundle-links.sh <bundle-dir>}
bundle=$(cd "$bundle" && pwd)
fail=0

is_macho() { file -b "$1" | grep -q 'Mach-O'; }

allowed_system() {
  case "$1" in
    /usr/lib/* | /System/Library/*) return 0 ;;
    *) return 1 ;;
  esac
}

resolves_in_bundle() { # <file> <dependency>
  local dir dep=$2
  dir=$(dirname "$1")
  case "$dep" in
    @executable_path/*) dep="$bundle/runtime/bin/${dep#@executable_path/}" ;;
    @loader_path/*) dep="$dir/${dep#@loader_path/}" ;;
    *) return 1 ;;
  esac
  [ -e "$dep" ]
}

check_file() {
  local file=$1 strict=$2 dep rel
  rel=${file#"$bundle"/}
  # otool -L prints the file name first, then one dependency per line.
  while read -r dep; do
    if allowed_system "$dep"; then
      continue
    fi
    if [ "$strict" = no ] && resolves_in_bundle "$file" "$dep"; then
      continue
    fi
    echo "::error::$rel links $dep (not a system library)" >&2
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
