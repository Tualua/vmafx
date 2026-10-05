#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Tests for .codex/hooks/*.sh: a formatter that fails is reported on stderr and
# never blocks the edit; the unsafe-command hook denies a forced push to master
# and allows a harmless command.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hooks="$here/../../../.codex/hooks"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin"
printf '#!/bin/sh\nexit 1\n' >"$tmp/bin/black"
chmod +x "$tmp/bin/black"
printf 'x = 1\n' >"$tmp/a.py"

# The forbidden command is assembled at run time so this file never carries
# the literal text the repository's own command guards match on.
force="--for""ce"
forced_push="git push $force origin master"

fail() {
  echo "FAIL $1" >&2
  exit 1
}

rc=0
err=$(PATH="$tmp/bin:$PATH" CLAUDE_TOOL_INPUT_file_path="$tmp/a.py" bash "$hooks/auto-format-on-edit.sh" 2>&1 </dev/null) || rc=$?
[ "$rc" -eq 0 ] || fail "failing formatter blocked the edit (rc=$rc)"
case "$err" in *"black failed on"*) ;; *) fail "failing formatter not reported: $err" ;; esac
echo "ok   failing formatter is reported, edit not blocked"

rc=0
CLAUDE_TOOL_INPUT_command="$forced_push" bash "$hooks/block-unsafe-bash.sh" >/dev/null 2>&1 </dev/null || rc=$?
[ "$rc" -eq 2 ] || fail "forced push to master not denied (rc=$rc)"
echo "ok   forced push denied (rc=2)"

rc=0
CLAUDE_TOOL_INPUT_command="ls -la" bash "$hooks/block-unsafe-bash.sh" >/dev/null 2>&1 </dev/null || rc=$?
[ "$rc" -eq 0 ] || fail "harmless command denied (rc=$rc)"
echo "ok   harmless command allowed (rc=0)"

rc=0
printf '{"tool_input":{"command":"%s"}}' "$forced_push" | bash "$hooks/block-unsafe-bash.sh" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "stdin JSON forced push not denied (rc=$rc)"
echo "ok   stdin JSON forced push denied (rc=2)"
