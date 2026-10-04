#!/usr/bin/env bash
# Verify the vmaf-mcp SBOMs against the wheel and sdist they describe.
#
# Usage: verify-mcp-sbom.sh VERSION SBOM_DIR DIST_DIR
#   VERSION   the SemVer spelling of the release (hatchling 1.32 writes the
#             pyproject spelling, 1.0.0-rc.2, into METADATA and Syft 1.51 copies
#             it verbatim into versionInfo and the purl; the PEP 440 spelling is
#             only for file names)
#   SBOM_DIR  holds vmaf-mcp.spdx.json and vmaf-mcp.cdx.json
#   DIST_DIR  holds the wheel and the sdist the SBOMs were generated from
#
# Checks, for each of the two documents: the root package carries the name,
# version and purl; the runtime dependencies are present and hang off the root;
# every file of DIST_DIR is listed with its SHA-256. Shared by the release
# workflow (supply-chain.yml, job `sbom`) and its pull-request dry run
# (release-dry-run.yml), so the check that gates a release is the one a pull
# request exercised.
#
# Exit status: 0 = every check passed, 1 = a check failed, 64 = bad usage.
#
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

set -euo pipefail

if [[ $# -ne 3 ]]; then
  printf 'Usage: verify-mcp-sbom.sh VERSION SBOM_DIR DIST_DIR\n' >&2
  exit 64
fi
VMAFX_VERSION="$1"
sbom_dir="$2"
dist_dir="$3"

for f in "$sbom_dir/vmaf-mcp.spdx.json" "$sbom_dir/vmaf-mcp.cdx.json"; do
  if [[ ! -f "$f" ]]; then
    printf 'ERROR: missing SBOM %s\n' "$f" >&2
    exit 1
  fi
done
if [[ ! -d "$dist_dir" ]]; then
  printf 'ERROR: %s is not a directory\n' "$dist_dir" >&2
  exit 1
fi

fail() {
  printf 'ERROR: %s\n' "$1" >&2
  exit 1
}

jq -e --arg version "$VMAFX_VERSION" '
  . as $doc
  | first(
      $doc.packages[]?
      | select(
          .name == "vmaf-mcp"
          and .versionInfo == $version
          and any(.externalRefs[]?;
            .referenceType == "purl"
            and .referenceLocator == ("pkg:pypi/vmaf-mcp@" + $version)))
      | .SPDXID
    ) as $root
  | ((["anyio", "mcp", "pydantic"] - [$doc.packages[]?.name]) == [])
    and any($doc.relationships[]?;
      .relationshipType == "DEPENDENCY_OF"
      and .relatedSpdxElement == $root)
' "$sbom_dir/vmaf-mcp.spdx.json" >/dev/null ||
  fail "vmaf-mcp.spdx.json: root package, runtime dependencies or dependency graph does not match $VMAFX_VERSION"

jq -e --arg version "$VMAFX_VERSION" '
  . as $doc
  | ("pkg:pypi/vmaf-mcp@" + $version) as $purl
  | ($doc.metadata.component.name == "vmaf-mcp")
    and ($doc.metadata.component.version == $version)
    and ((["anyio", "mcp", "pydantic"] - [$doc.components[]?.name]) == [])
    and any($doc.components[]?; .purl == $purl)
    and any($doc.dependencies[]?;
      (((.ref // "") == $purl) or ((.ref // "") | startswith($purl + "?")))
      and ((.dependsOn // []) | length) >= 3)
' "$sbom_dir/vmaf-mcp.cdx.json" >/dev/null ||
  fail "vmaf-mcp.cdx.json: root component, runtime dependencies or dependency graph does not match $VMAFX_VERSION"

checked=0
for f in "$dist_dir"/*; do
  [[ -f "$f" ]] || continue
  name="$(basename "$f")"
  sha="$(sha256sum -- "$f" | cut -d" " -f1)"
  jq -e --arg name "$name" --arg sha "$sha" '
    any(.files[]?;
      ((.fileName // "") | endswith("/" + $name))
      and any(.checksums[]?;
        .algorithm == "SHA256" and .checksumValue == $sha))
  ' "$sbom_dir/vmaf-mcp.spdx.json" >/dev/null ||
    fail "vmaf-mcp.spdx.json does not list $name with SHA-256 $sha"
  jq -e --arg name "$name" --arg sha "$sha" '
    any(.components[]?;
      .type == "file"
      and ((.name // "") | endswith("/" + $name))
      and any(.hashes[]?;
        .alg == "SHA-256" and .content == $sha))
  ' "$sbom_dir/vmaf-mcp.cdx.json" >/dev/null ||
    fail "vmaf-mcp.cdx.json does not list $name with SHA-256 $sha"
  checked=$((checked + 1))
done
if [[ "$checked" -eq 0 ]]; then
  fail "$dist_dir holds no distribution file to check"
fi
printf 'OK: vmaf-mcp SBOMs match %s and %d distribution file(s)\n' "$VMAFX_VERSION" "$checked"
