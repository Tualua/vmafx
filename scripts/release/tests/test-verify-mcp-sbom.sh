#!/usr/bin/env bash
# Regression tests for scripts/release/verify-mcp-sbom.sh.
#
# Builds Syft-shaped SPDX and CycloneDX documents for a fake wheel and sdist,
# proves the script accepts them, then plants one defect at a time and proves
# the script refuses each (a check never seen failing is not a check). Needs
# jq and python3; no Syft, no network.
#
# Usage: bash scripts/release/tests/test-verify-mcp-sbom.sh
#
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

set -euo pipefail

for tool in jq python3 sha256sum; do
  if ! command -v "$tool" >/dev/null; then
    printf 'ERROR: %s is required to run these tests\n' "$tool" >&2
    exit 1
  fi
done

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VERIFY="$SCRIPT_DIR/../verify-mcp-sbom.sh"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT INT TERM
pass=0
fail=0
ok() {
  pass=$((pass + 1))
  printf 'ok   %s\n' "$1"
}
bad() {
  fail=$((fail + 1))
  printf 'FAIL %s\n' "$1" >&2
}

version="1.0.0-rc.2"
dist="$scratch/dist"
mkdir -p "$dist"
printf 'wheel bytes' >"$dist/vmaf_mcp-1.0.0rc2-py3-none-any.whl"
printf 'sdist bytes' >"$dist/vmaf_mcp-1.0.0rc2.tar.gz"

# make_sboms DIR [VERSION_IN_SBOM] writes the two documents Syft would for DIST.
make_sboms() {
  python3 - "$1" "${2:-$version}" "$dist" <<'PY'
import hashlib, json, os, sys

out, ver, dist = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
purl = f"pkg:pypi/vmaf-mcp@{ver}"
files = sorted(os.listdir(dist))
sha = {f: hashlib.sha256(open(os.path.join(dist, f), "rb").read()).hexdigest() for f in files}
deps = ["anyio", "mcp", "pydantic"]
spdx = {
    "spdxVersion": "SPDX-2.3",
    "name": "vmaf-mcp",
    "packages": [
        {"SPDXID": "SPDXRef-root", "name": "vmaf-mcp", "versionInfo": ver,
         "externalRefs": [{"referenceType": "purl", "referenceLocator": purl}]},
    ] + [{"SPDXID": f"SPDXRef-{d}", "name": d, "versionInfo": "1"} for d in deps],
    "relationships": [
        {"spdxElementId": f"SPDXRef-{d}", "relationshipType": "DEPENDENCY_OF",
         "relatedSpdxElement": "SPDXRef-root"} for d in deps],
    "files": [{"fileName": f"./distributions/{f}",
               "checksums": [{"algorithm": "SHA256", "checksumValue": sha[f]}]} for f in files],
}
cdx = {
    "bomFormat": "CycloneDX",
    "metadata": {"component": {"name": "vmaf-mcp", "version": ver}},
    "components": [{"type": "library", "name": "vmaf-mcp", "purl": purl}]
    + [{"type": "library", "name": d} for d in deps]
    + [{"type": "file", "name": f"distributions/{f}",
        "hashes": [{"alg": "SHA-256", "content": sha[f]}]} for f in files],
    "dependencies": [{"ref": purl, "dependsOn": deps}],
}
json.dump(spdx, open(os.path.join(out, "vmaf-mcp.spdx.json"), "w"))
json.dump(cdx, open(os.path.join(out, "vmaf-mcp.cdx.json"), "w"))
PY
}

# mutate DIR FILE JQ-FILTER rewrites one document.
mutate() {
  jq -c "$3" "$1/$2" >"$1/$2.new"
  mv "$1/$2.new" "$1/$2"
}

expect_pass() {
  local description="$1" dir="$2" rc=0
  "$VERIFY" "$version" "$dir" "$dist" >"$scratch/out" 2>"$scratch/err" || rc=$?
  if [[ "$rc" -eq 0 ]]; then ok "$description"; else bad "$description (exit $rc: $(cat "$scratch/err"))"; fi
}

expect_refused() {
  local description="$1" dir="$2" rc=0
  "$VERIFY" "$version" "$dir" "$dist" >"$scratch/out" 2>"$scratch/err" || rc=$?
  if [[ "$rc" -eq 1 && -s "$scratch/err" ]]; then ok "$description"; else bad "$description (expected exit 1 with a message, got $rc)"; fi
}

good="$scratch/good"
make_sboms "$good"
expect_pass "accepts the SBOMs of the distribution files" "$good"

d="$scratch/spdx-version"
make_sboms "$d" 1.0.0rc2
expect_refused "refuses an SPDX root spelled with the PEP 440 version" "$d"

d="$scratch/spdx-dep"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.spdx.json '.packages |= map(select(.name != "pydantic"))'
expect_refused "refuses an SPDX document without a runtime dependency" "$d"

d="$scratch/spdx-graph"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.spdx.json '.relationships = []'
expect_refused "refuses an SPDX document with no dependency relationship" "$d"

d="$scratch/spdx-hash"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.spdx.json '.files[0].checksums[0].checksumValue = ("0" * 64)'
expect_refused "refuses an SPDX file entry with another SHA-256" "$d"

d="$scratch/spdx-missing-file"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.spdx.json '.files |= .[1:]'
expect_refused "refuses an SPDX document that omits a distribution file" "$d"

d="$scratch/cdx-version"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.cdx.json '.metadata.component.version = "9.9.9"'
expect_refused "refuses a CycloneDX root of another version" "$d"

d="$scratch/cdx-deps"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.cdx.json '.dependencies[0].dependsOn = ["anyio"]'
expect_refused "refuses a CycloneDX graph with fewer than three dependencies" "$d"

d="$scratch/cdx-hash"
cp -r "$good" "$d"
mutate "$d" vmaf-mcp.cdx.json '(.components[] | select(.type == "file") | .hashes[0].content) |= "deadbeef"'
expect_refused "refuses a CycloneDX file component with another SHA-256" "$d"

d="$scratch/absent"
mkdir -p "$d"
expect_refused "refuses a missing SBOM" "$d"

empty="$scratch/empty-dist"
mkdir -p "$empty"
rc=0
"$VERIFY" "$version" "$good" "$empty" >/dev/null 2>"$scratch/err" || rc=$?
if [[ "$rc" -eq 1 ]]; then ok "refuses an empty distribution directory"; else bad "empty distribution directory exited $rc"; fi

rc=0
"$VERIFY" "$version" "$good" >/dev/null 2>&1 || rc=$?
if [[ "$rc" -eq 64 ]]; then ok "exits 64 on a wrong argument count"; else bad "wrong argument count exited $rc"; fi

printf '%d passed, %d failed\n' "$pass" "$fail"
[[ "$fail" -eq 0 ]]
