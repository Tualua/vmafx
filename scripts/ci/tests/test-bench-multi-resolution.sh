#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Tests for scripts/perf/bench-multi-resolution.sh against a fake `vmaf`: a
# working binary yields an "ok" cell with the pooled score, the feature score and
# a median time; a binary that exits non-zero yields a "skip" cell that names the
# exit status and carries the binary's stderr.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../../perf/bench-multi-resolution.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL $1" >&2
  exit 1
}

ws="$tmp/ws"
mkdir -p "$ws/testdata" "$ws/model"
: >"$ws/testdata/ref_576x324_48f.yuv"
: >"$ws/testdata/dis_576x324_48f.yuv"
: >"$ws/model/vmaf_v0.6.1.json"

cat >"$tmp/vmaf" <<'SH'
#!/usr/bin/env bash
out=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output) out="$2"; shift 2 ;;
    *) shift ;;
  esac
done
if [ -n "${FAKE_VMAF_FAIL:-}" ]; then
  echo "fake vmaf: simulated failure" >&2
  exit 3
fi
printf '{"pooled_metrics":{"vmaf":{"mean":80.5},"vif_scale0":{"mean":0.25}}}\n' >"$out"
SH
chmod +x "$tmp/vmaf"

run_bench() {
  VMAF_BIN="$tmp/vmaf" VMAF_ROOT="$ws" VMAF_ONEAPI_SETVARS="" ONEAPI_ROOT="$tmp/none" \
    bash "$script" --resolutions 576 --metrics vif --backends cpu --runs 2 \
    --output "$tmp/out.json" >"$tmp/stdout.txt" 2>"$tmp/stderr.txt"
}

field() { # $1 python expression over the first cell `c`
  python3 - "$tmp/out.json" "$1" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
cells = doc["runs"]
c = cells[0]
print(eval(sys.argv[2]))
PY
}

run_bench || fail "bench failed: $(cat "$tmp/stderr.txt")"
[ "$(field 'c["status"]')" = ok ] || fail "working vmaf: status $(field 'c["status"]')"
[ "$(field 'c["vmaf_score"]')" = 80.5 ] || fail "working vmaf: vmaf_score $(field 'c["vmaf_score"]')"
[ "$(field 'c["feature_score"]')" = 0.25 ] || fail "working vmaf: feature_score $(field 'c["feature_score"]')"
[ "$(field 'c["median_ms"] is not None')" = True ] || fail "working vmaf: no median time"
echo "ok   a working vmaf yields an ok cell with scores and a median"

FAKE_VMAF_FAIL=1 run_bench || fail "bench aborted on a failing vmaf: $(cat "$tmp/stderr.txt")"
[ "$(field 'c["status"]')" = skip ] || fail "failing vmaf: status $(field 'c["status"]')"
[ "$(field 'c["skip_reason"]')" = "vmaf exited 3" ] || fail "failing vmaf: skip_reason $(field 'c["skip_reason"]')"
case "$(field 'c["vmaf_error"]')" in *"simulated failure"*) ;; *) fail "failing vmaf: stderr not carried" ;; esac
echo "ok   a failing vmaf yields a skip cell naming the status and stderr"
