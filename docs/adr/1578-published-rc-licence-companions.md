<!-- markdownlint-disable MD013 MD060 -->
# ADR-1578: The rc.1 and rc.2 ROCm and node images are withdrawn; every other published rc image gets notices and a source companion

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, release, docker, supply-chain, fork-local

## Context

Everything published for v1.0.0-rc.1 and rc.2 predates
[ADR-1513](1513-production-artifact-licensing.md)
([Research-2140](../research/2140-production-artifact-licence-audit.md),
`T-PROD-LICENCE-PUBLISHED-RC-ARTIFACTS-2026-10-04`). No image carries the
notices of what it contains or the source of its copyleft parts. Two kinds of
image cannot be fixed by adding anything:

- the `-rocm10` images redistribute `librocprof-trace-decoder.so`, whose AMD
  licence forbids distribution;
- the `vmafx-node` images ship an FFmpeg configured `--enable-nonfree`, which
  declares itself not legally redistributable.

Registry digests and release files are immutable, so a fix can add things next
to an image but cannot change it.

## Decision

The `-rocm10` and `vmafx-node` images of both releases are withdrawn: their
tags, their platform and attestation manifests, their signature and referrer
indexes, and the untagged images of earlier runs of the same recipe are
deleted from GHCR. The orchestrator deletes them with a token that has
`delete:packages`; the list of version ids is evidence of this change. A
licence section on each release page says what was withdrawn and why.

Every other image of the two releases stays unchanged and is completed after
the fact. The images, their digests, platforms and licence records are listed
in `tools/rc1-tester/image/published-rc/artifacts.json`. The manual workflow
`published-rc-licence-companions.yml` does the following for each image:

1. It refuses to run if the tag no longer names the recorded digest.
2. It unpacks every platform and writes the notices with `licensing.py`. The
   records are `published-rc-*`; they take the production records' components
   by reference, add the vendor packages of the old Ubuntu bases by pattern,
   and add the texts the old images lack.
3. It fetches the corresponding source into `<tag>-source`, signs it and
   attests it, and it attests an SPDX SBOM on the published digest.
4. It attaches the notices to the release page and writes the page's licence
   section.

The native release files and `models.tar.gz` get their notices on the same
pages. The VMAFx part of the notices comes from licence scans of the release
tags' builds (CPU, CUDA, SYCL, and the Go programs), recorded under
`published-rc/scans/`. A GPU build compiles files the CPU build does not, so a
CPU scan alone would understate the GPU images. Ubuntu packages at superseded
versions are fetched from Launchpad, which `licensing.py` now uses after the
archive and snapshot.debian.org.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Withdraw the ROCm and node images, complete the rest (chosen) | rc.1 and rc.2 stay reproducible by digest; only the images that cannot comply disappear | A workflow and records for images nobody will build again | Maintainer's choice |
| Withdraw every rc.1 and rc.2 image and rely on rc.3 | Nothing to complete | Breaks every pinned digest and the testers' reports that name them | Not chosen |
| Complete the ROCm and node images too | Nothing deleted | A notice cannot permit distributing a file whose licence forbids it, or make a self-declared nonfree build redistributable | Not possible |
| Build the GPU scans in the workflow | No recorded scans | CUDA and oneAPI toolchains on hosted runners for one-off scans of immutable tags; the scans are facts about fixed commits | The scans are recorded with the command that regenerates them |
| Publish the notices only, without source images | Small | The GPL and LGPL parts need their source at the same place as the image | Same reasoning as ADR-1513 |

## Consequences

- **Positive**: every rc image that stays has its notices, its source and an
  SBOM, and anyone who pinned a digest keeps working; the two images that could
  not comply are gone.
- **Negative**: pulls of the withdrawn tags fail. The whole `vmafx-node`
  package was deleted, because every version came from the nonfree recipe and
  GHCR refuses to delete a package's last version; the rc.3 node image starts a
  new package, which is private until made public.
- **Neutral / follow-ups**: carried out on 2026-10-04 by the orchestrator: the
  76 listed versions and 34 untagged images built from `master` deleted (GHCR
  can restore them for 30 days), the `vmafx-node` package deleted, and
  `vmaf-mcp` 1.0.0rc1 and 1.0.0rc2 yanked on PyPI (the maintainer's decision;
  PyPI files cannot be changed). The workflow is then dispatched for both
  releases with `publish`, which closes the state row.

## References

- `Q` (popup 2026-10-04, published rc.1 / rc.2 artifacts): "Withdraw 2, fix the rest (Recommended)".
- [ADR-1513](1513-production-artifact-licensing.md), [ADR-1517](1517-gpu-image-licensing.md), [ADR-1514](1514-go-and-node-image-licensing.md), [ADR-1503](1503-tester-artifact-licensing.md), [Research-2140](../research/2140-production-artifact-licence-audit.md).
- GitHub REST API, "Delete package version for an organization" (scopes `read:packages`, `delete:packages`; restorable for 30 days; read 2026-10-04); Launchpad API `getPublishedSources` / `sourceFileUrls` (read 2026-10-04).
