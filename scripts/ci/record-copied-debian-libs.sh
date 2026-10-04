#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# Record the shared libraries an image copies out of Debian packages (the node
# image's FFmpeg dependency closure) so that their licences travel with them
# (ADR-1513): for each library path in ORIGINS that a dpkg package owns, write
#   <PREFIX>/<basename> <package> <version> <source>=<source version>
# to OUT_DIR/packages.list and copy the package's copyright file to
# OUT_DIR/<package>/copyright. Libraries no package owns (built from source in
# the image) are printed and left to their own licence record. The list is the
# `dpkg-copied` component of tools/rc1-tester/image/licensing.json.
#
# Usage: record-copied-debian-libs.sh ORIGINS OUT_DIR PREFIX
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: record-copied-debian-libs.sh ORIGINS OUT_DIR PREFIX" >&2
  exit 2
fi
origins="$1"
out="$2"
prefix="${3%/}"
mkdir -p "$out"
: >"$out/packages.list"

owner_of() {
  # dpkg records the merged-/usr path a package installed; ldd may report the
  # /lib alias, and the copy keeps the SONAME, so ask for the resolved file.
  local real candidate owner
  real="$(realpath "$1")"
  for candidate in "$real" "${real#/usr}"; do
    if owner="$(dpkg-query -S "$candidate" 2>/dev/null)"; then
      printf '%s\n' "${owner%%:*}"
      return 0
    fi
  done
  return 1
}

count=0
while IFS= read -r library; do
  count=$((count + 1))
  if [[ "$count" -gt 4096 ]]; then
    echo "more than 4096 libraries in $origins" >&2
    exit 1
  fi
  [[ -n "$library" ]] || continue
  if ! package="$(owner_of "$library")"; then
    echo "not from a Debian package (recorded elsewhere): $library"
    continue
  fi
  fields="$(dpkg-query -W -f='${Version} ${source:Package}=${source:Version}' "$package")"
  printf '%s/%s %s %s\n' "$prefix" "$(basename "$library")" "$package" "$fields" >>"$out/packages.list"
  mkdir -p "$out/$package"
  cp -L "/usr/share/doc/$package/copyright" "$out/$package/copyright"
done <"$origins"

if [[ ! -s "$out/packages.list" ]]; then
  echo "no library of $origins comes from a Debian package; the record would be empty" >&2
  exit 1
fi
