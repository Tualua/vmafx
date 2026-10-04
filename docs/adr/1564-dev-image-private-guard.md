<!-- markdownlint-disable MD013 MD060 -->
# ADR-1564: The dev container is pushed only into a private package, checked before every push

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, ci, docker, supply-chain, fork-local

## Context

`dev-container-publish.yml` builds the `libvmaf-build` stage of
`dev/Containerfile` and pushes it as `ghcr.io/vmafx/vmafx-dev-mcp` (87 versions
on 2026-10-04). The stage holds the full CUDA toolkit
(`install-cuda-toolkit.sh --mode=full`: `nvcc`, headers and tools, not only the
Attachment A runtime), Intel's oneAPI Base Kit and the ROCm payload. The CUDA
EULA (1.1.1, Attachment A) and the Intel EULA for Developer Tools (2.1) allow
internal use of these files and allow redistribution only of the listed
runtime parts. The package is private and the organisation has one member, so
today the image is internal use
([Research-2140](../research/2140-production-artifact-licence-audit.md), gap 18,
`T-PROD-LICENCE-DEV-CONTAINER-2026-10-04`). Nothing stopped the package from
being made public, or the workflow from pushing into one that was. Package
visibility is a setting in the GitHub UI, outside the repository, so a review
of the repository cannot see it.

## Decision

Before it builds anything, the publish job runs
`scripts/ci/require-private-ghcr-package.sh VMAFx vmafx-dev-mcp`. The script
reads `GET /orgs/VMAFx/packages/container/vmafx-dev-mcp` with `gh api` and the
job's token, and exits 0 only when `visibility` is exactly `private`. It fails
closed: an API error (no such package, a token that may not read it, a
rate limit), an answer without a string `visibility`, or `public` or
`internal` stops the job before login, build and push. The step has no `if:`
and no `continue-on-error`.

GitHub's documentation for the endpoint names only classic tokens with
`read:packages`. The job token reads a container package whose Actions access
grants the repository a role, which is how `actions/delete-package-versions`
uses it. If it cannot read this one, the first run fails and the fix is a
token that can, not a weaker guard.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Refuse to push unless the package is private (chosen) | Keeps the image for internal use; a visibility change or a wrong token stops the next push | One API call per run; a token without package read fails the job | Maintainer's choice |
| Stop publishing the stage | Nothing to keep private | Contributors lose the prebuilt image; every dev container is built locally (about 30 minutes, 29.5 GB) | Not chosen; the image is useful internally |
| Publish only the redistributable parts | Could be public | A different image (no `nvcc`, no compilers) that no longer builds VMAFx | Not what the image is for |
| Check the visibility after the push | Push not delayed | The files are already distributed when the check fails | Too late |
| Document the rule, no check | No code | Visibility is set outside the repository; nothing would notice | A rule with no check |

## Consequences

- **Positive**: the dev image cannot reach a public package through this
  workflow, and an unreadable visibility is treated as a refusal, not as a pass.
- **Negative**: a token or API change that hides package metadata blocks the
  dev image publish until it is fixed.
- **Neutral / follow-ups**: the versions already in the package are private
  and stay; the check does not cover a package made public by hand between
  two pushes. The documentation no longer describes the image as published
  "for transparency".

## References

- `Q` (popup 2026-10-04, dev image): "Refuse push unless private (Recommended)".
- [ADR-1513](1513-production-artifact-licensing.md), [ADR-1503](1503-tester-artifact-licensing.md), [Research-2140](../research/2140-production-artifact-licence-audit.md).
- GitHub REST API, "Get a package for an organization" (`visibility`: `private` or `public`; read 2026-10-04); `actions/delete-package-versions` README (job token and Actions access roles; read 2026-10-04).
