#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Tests for the msvcism stage of scripts/dev/preflight.sh in a throwaway repo:
# a tree with no hostile construct passes, and each planted construct (a
# `nullptr` in a C file, a parenthesised __attribute__) makes the stage fail
# with the finding printed. Run with `set -e`, a probe that finds nothing must
# not end the script, and a probe that finds something must still be reported.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
preflight="$here/../../dev/preflight.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL $1" >&2
  exit 1
}

cd "$tmp"
git init -q
git config user.email t@t
git config user.name t
printf 'int main(void) { return 0; }\n' >ok.c
git add ok.c
git commit -qm init

run_stage() {
  PREFLIGHT_BASE=HEAD bash "$preflight" --stage msvcism 2>&1
}

rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 0 ] || fail "clean tree failed (rc=$rc): $out"
case "$out" in *"PASS"*msvcism*) ;; *) fail "clean tree did not report PASS: $out" ;; esac
echo "ok   a tree without hostile constructs passes"

printf 'void *p = nullptr;\n' >bad_nullptr.c
rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 1 ] || fail "nullptr in a C file: expected rc=1, got $rc: $out"
case "$out" in *"nullptr in a C translation unit"*) ;; *) fail "nullptr finding not printed: $out" ;; esac
rm bad_nullptr.c
echo "ok   a nullptr in a C file fails the stage and is reported"

printf '__attribute__(packed) int x;\n' >bad_attr.c
rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 1 ] || fail "__attribute__(x): expected rc=1, got $rc: $out"
case "$out" in *"__attribute__ with single parens"*) ;; *) fail "attribute finding not printed: $out" ;; esac
echo "ok   a single-paren __attribute__ fails the stage and is reported"
