#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Tests for the msvcism stage of scripts/dev/preflight.sh in a throwaway repo:
# a tree with no hostile construct passes, and each planted construct (a
# `nullptr` in a C file, a parenthesised __attribute__, a POSIX-only header
# outside a platform conditional in a source the Windows build compiles) makes
# the stage fail with the finding printed; a guarded include and a source a
# meson.build keeps off Windows pass, and a POSIX-header scan that cannot run
# fails the stage. Run with `set -e`, a probe that finds nothing must
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
rm bad_attr.c

printf '#include <unistd.h>\nint f(void) { return 0; }\n' >bad_posix.c
rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 1 ] || fail "unguarded <unistd.h>: expected rc=1, got $rc: $out"
case "$out" in *"POSIX-only header outside a platform conditional"*"bad_posix.c:1: <unistd.h>"*) ;;
*) fail "POSIX header finding not printed: $out" ;; esac
echo "ok   an unguarded <unistd.h> in a source the Windows build compiles fails the stage"

printf '#ifndef _WIN32\n#include <unistd.h>\n#endif\nint f(void) { return 0; }\n' >bad_posix.c
rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 0 ] || fail "guarded <unistd.h>: expected rc=0, got $rc: $out"
echo "ok   a <unistd.h> under #ifndef _WIN32 passes"

printf '#include <sys/socket.h>\nint g(void) { return 0; }\n' >posix_tool.c
printf "if host_machine.system() != 'windows'\n  executable('posix_tool', 'posix_tool.c')\nendif\n" >meson.build
rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 0 ] || fail "source kept off Windows: expected rc=0, got $rc: $out"
printf "executable('posix_tool', 'posix_tool.c')\n" >meson.build
rc=0
out="$(run_stage)" || rc=$?
[ "$rc" -eq 1 ] || fail "source built on Windows: expected rc=1, got $rc: $out"
case "$out" in *"posix_tool.c:1: <sys/socket.h>"*) ;; *) fail "meson-built finding not printed: $out" ;; esac
rm meson.build posix_tool.c
echo "ok   a meson.build gate off Windows exempts a source, an ungated target does not"

# A scanner that cannot run must fail the stage, not pass it for want of output.
mkdir -p fakebin
printf '#!/bin/sh\nexit 3\n' >fakebin/python3
chmod +x fakebin/python3
rc=0
out="$(PATH="$tmp/fakebin:$PATH" run_stage)" || rc=$?
[ "$rc" -eq 1 ] || fail "failing scanner: expected rc=1, got $rc: $out"
case "$out" in *"POSIX-only header scan did not run"*) ;; *) fail "scanner failure not reported: $out" ;; esac
rm -r fakebin
echo "ok   a POSIX-header scan that cannot run fails the stage"
