#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Tests for dev/scripts/smoke-probe-loop.sh helpers, run against a fake `vmaf`
# and a fake `vmafx-mcp` on PATH: probe_backend returns the pooled score when
# the output names the requested backend and a null score with an error when it
# does not; _mcp_call performs the initialize handshake and returns the reply.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../../../dev/scripts/smoke-probe-loop.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin"

cat >"$tmp/bin/vmaf" <<'SH'
#!/usr/bin/env bash
out="" backend=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output) out="$2"; shift 2 ;;
    --backend) backend="$2"; shift 2 ;;
    *) shift ;;
  esac
done
used="${FAKE_BACKEND_USED:-$backend}"
printf '{"pooled_metrics":{"vmaf":{"mean":91.25}},"backend_used":"%s"}\n' "$used" >"$out"
SH
cat >"$tmp/bin/vmafx-mcp" <<'PY'
#!/usr/bin/env python3
import json
import sys

for line in sys.stdin:
    msg = json.loads(line)
    if msg.get("id") == 2:
        text = json.dumps({"extractors": ["psnr", "vif"]})
        reply = {"jsonrpc": "2.0", "id": 2, "result": {"content": [{"type": "text", "text": text}]}}
        print(json.dumps(reply), flush=True)
        break
PY
chmod +x "$tmp/bin/vmaf" "$tmp/bin/vmafx-mcp"
export PATH="$tmp/bin:$PATH"
export PROBE_OUTPUT_DIR="$tmp/out"

fail() {
  echo "FAIL $1" >&2
  exit 1
}

# shellcheck source=/dev/null
source "$script"
sep=$'\x1f'

res="$(probe_backend cpu)"
score="${res%%"$sep"*}"
[ "$score" = "91.25" ] || fail "probe_backend cpu score '$score', expected 91.25"
echo "ok   probe_backend returns the pooled score of the requested backend"

res="$(FAKE_BACKEND_USED=cpu probe_backend cuda)"
score="${res%%"$sep"*}"
err="${res##*"$sep"}"
[ "$score" = "null" ] || fail "backend mismatch gave score '$score', expected null"
[ "$err" != "null" ] || fail "backend mismatch gave no error"
echo "ok   probe_backend rejects a run that used another backend"

res="$(probe_backend bogus)"
case "$res" in "null${sep}0${sep}"*"unknown backend"*) ;; *) fail "unknown backend not rejected: $res" ;; esac
echo "ok   probe_backend rejects an unknown backend"

reply="$(_mcp_call list_extractors '{}')"
printf '%s' "$reply" | python3 -c 'import json, sys; d = json.load(sys.stdin); assert d["id"] == 2 and "result" in d' ||
  fail "_mcp_call reply lacks the tools/call result: $reply"
echo "ok   _mcp_call returns the tools/call reply"
