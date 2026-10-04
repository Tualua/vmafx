#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Tests for scripts/ci/record-copied-debian-libs.sh against the host's real
# dpkg database (ADR-1513, ADR-1593). Positive: a one-field origin line is
# recorded under PREFIX, a two-field line under its own directory, and the
# package's copyright file is copied. Negative: a relative destination, a line
# with three fields, a list with no package-owned file and a wrong argument
# count fail. Boundary: an unowned file next to an owned one is reported and
# skipped; a destination with a trailing slash records no double slash.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../record-copied-debian-libs.sh"
if ! command -v dpkg-query >/dev/null 2>&1; then
  echo "SKIP: dpkg-query is not on PATH; the recorder reads a Debian or Ubuntu dpkg database"
  exit 0
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

owned="$(realpath /bin/sh)"
package="$(dpkg-query -S "$owned" | head -n 1)"
package="${package%%:*}"
fields="$(dpkg-query -W -f='${Version} ${source:Package}=${source:Version}' "$package")"
base="$(basename "$owned")"
fail=0

check() { # $1 label, $2 expected rc, $3 origins text, [$4 expected list, [$5 stderr needle]]
  local rc=0
  printf '%s' "$3" >"$tmp/origins"
  rm -rf "$tmp/out"
  bash "$script" "$tmp/origins" "$tmp/out" /usr/local/lib >"$tmp/stdout" 2>"$tmp/stderr" || rc=$?
  if [ "$rc" -ne "$2" ]; then
    echo "FAIL $1: rc=$rc, want $2 ($(cat "$tmp/stderr"))" >&2
    fail=1
    return
  fi
  if [ -n "${4:-}" ] && [ "$(cat "$tmp/out/packages.list")" != "$4" ]; then
    echo "FAIL $1: packages.list is '$(cat "$tmp/out/packages.list")', want '$4'" >&2
    fail=1
    return
  fi
  if [ -n "${5:-}" ] && ! grep -qF -- "$5" "$tmp/stderr" "$tmp/stdout"; then
    echo "FAIL $1: output lacks '$5'" >&2
    fail=1
    return
  fi
  echo "PASS $1"
}

check "one field: recorded under PREFIX" 0 "$owned"$'\n' "/usr/local/lib/$base $package $fields"
if [ ! -s "$tmp/out/$package/copyright" ]; then
  echo "FAIL the copyright file of $package was not copied" >&2
  fail=1
fi
check "two fields: recorded under the line's directory" 0 "$owned /usr/bin"$'\n' "/usr/bin/$base $package $fields"
check "trailing slash: no double slash" 0 "$owned /opt/tools/"$'\n' "/opt/tools/$base $package $fields"
touch "$tmp/unowned"
check "unowned next to owned: skipped and reported" 0 "$tmp/unowned"$'\n'"$owned"$'\n' \
  "/usr/local/lib/$base $package $fields" "not from a Debian package (recorded elsewhere): $tmp/unowned"
check "relative destination refused" 1 "$owned usr/bin"$'\n' "" "is not an absolute directory"
check "three fields refused" 1 "$owned /usr/bin extra"$'\n' "" "more than two fields"
check "nothing package-owned refused" 1 "$tmp/unowned"$'\n' "" "the record would be empty"
rc=0
bash "$script" "$tmp/origins" "$tmp/out" >/dev/null 2>&1 || rc=$?
if [ "$rc" -ne 2 ]; then
  echo "FAIL two arguments: rc=$rc, want 2" >&2
  fail=1
else
  echo "PASS two arguments refused"
fi
exit "$fail"
