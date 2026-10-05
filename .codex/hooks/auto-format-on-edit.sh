#!/usr/bin/env bash
# SPDX-License-Identifier: EUPL-1.2
# PostToolUse hook (matcher: Edit|Write): auto-format files after the agent edits them.
# Uses repo-local tool versions when available; silently skips if a formatter is not installed.
set -euo pipefail

# Claude Code passes hook input as JSON on stdin. Parse tool_input.file_path
# from it. Older docs referenced CLAUDE_TOOL_INPUT_file_path env var; keep
# it as a fallback for compatibility with any external caller.
file="${CLAUDE_TOOL_INPUT_file_path:-}"
if [[ -z "$file" ]] && command -v jq >/dev/null 2>&1; then
  # Read stdin (non-blocking — the hook runtime always provides it).
  input=$(cat 2>/dev/null) || input=""
  if [[ -n "$input" ]]; then
    file=$(printf '%s' "$input" | jq -r '.tool_input.file_path // empty' 2>/dev/null) || file=""
  fi
fi

[[ -z "$file" || ! -f "$file" ]] && exit 0

# Never reformat files that are explicitly upstream-touched or generated.
# Vendored verbatim mirrors (pelorus interop ABI, ADR-1113) must stay
# byte-identical to their single source of truth — clang-format would
# rewrap them and break the sync guard (scripts/sync-pelorus-interop.sh).
case "$file" in
  */subprojects/* | */build/* | */testdata/*.yuv | *.json | *.onnx | *.pkl)
    exit 0
    ;;
  */core/src/interop/pelorus_* | */core/include/libvmaf/pelorus/*)
    exit 0
    ;;
esac

case "$file" in
  *.c | *.h | *.cpp | *.hpp | *.cu | *.cuh)
    if command -v clang-format >/dev/null 2>&1; then
      if ! clang-format -i --style=file "$file"; then echo "clang-format failed on $file" >&2; fi
    fi
    ;;
  *.py)
    if command -v black >/dev/null 2>&1; then
      if ! black -q "$file"; then echo "black failed on $file" >&2; fi
    fi
    if command -v isort >/dev/null 2>&1; then
      if ! isort -q "$file"; then echo "isort failed on $file" >&2; fi
    fi
    ;;
  *.sh)
    if command -v shfmt >/dev/null 2>&1; then
      if ! shfmt -w -i 2 -ci "$file"; then echo "shfmt failed on $file" >&2; fi
    fi
    ;;
esac

exit 0
