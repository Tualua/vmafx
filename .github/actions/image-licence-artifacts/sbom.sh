#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
#
# SPDX SBOM (Syft) of every linux/amd64 and linux/arm64 image behind DIGEST,
# written to sbom-NAME-ARCH.spdx.json; the platform digests go to GITHUB_OUTPUT
# for the attest steps (ADR-1503 rule 6). Inputs: SYFT, IMAGE, DIGEST, NAME.
set -euo pipefail
: "${SYFT:?}" "${IMAGE:?}" "${DIGEST:?}" "${NAME:?}"

[[ "$DIGEST" =~ ^sha256:[0-9a-f]{64}$ ]] || {
  echo "bad digest: $DIGEST" >&2
  exit 1
}
raw="$(docker buildx imagetools inspect --raw "${IMAGE}@${DIGEST}")"
media="$(jq -r '.mediaType // empty' <<<"$raw")"

declare -A platform_digest=()
case "$media" in
  application/vnd.oci.image.index.v1+json | application/vnd.docker.distribution.manifest.list.v2+json)
    while IFS=$'\t' read -r arch digest; do
      platform_digest["$arch"]="$digest"
    done < <(jq -r '.manifests[] | select(.platform.os == "linux")
                    | select(.platform.architecture == "amd64" or .platform.architecture == "arm64")
                    | [.platform.architecture, .digest] | @tsv' <<<"$raw")
    ;;
  *)
    arch="$(docker buildx imagetools inspect "${IMAGE}@${DIGEST}" --format '{{json .Image}}' |
      jq -r '.architecture')"
    platform_digest["$arch"]="$DIGEST"
    ;;
esac

if [[ "${#platform_digest[@]}" -eq 0 ]]; then
  echo "no linux/amd64 or linux/arm64 image behind ${IMAGE}@${DIGEST}" >&2
  exit 1
fi
for arch in "${!platform_digest[@]}"; do
  digest="${platform_digest[$arch]}"
  out="sbom-${NAME}-${arch}.spdx.json"
  "$SYFT" scan "registry:${IMAGE}@${digest}" --platform "linux/${arch}" \
    --source-name "$IMAGE" -o "spdx-json=${out}"
  jq -e '.spdxVersion and ((.packages | length) > 10)' "$out" >/dev/null
  echo "${arch}=${digest}" >>"$GITHUB_OUTPUT"
done
