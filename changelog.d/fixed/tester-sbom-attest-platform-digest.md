- **The tester image's SPDX SBOM verifies on the digest you pull.** It was
  attested on a per-arch index that `imagetools create` does not publish, so
  `gh attestation verify` on the platform digest of the tag found nothing. It is
  now attested on each platform manifest the tag's index lists, and the
  publishing run verifies it. Commands:
  [tester guide](docs/usage/tester-image.md#licences-of-what-you-download).
