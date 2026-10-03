<!-- markdownlint-disable MD013 MD060 -->
# ADR-1492: Tester image with one report command, hardware reports as tracked files

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: maintainer, agent
- **Tags**: ci, docker, testing, arm64, rc7, docs, fork-local

## Context

Every aarch64 result of the fork so far comes from qemu-user on an x86 host. An
outside tester with an Apple M4 can run a Linux arm64 container (Docker Desktop's VM)
and so exercise the fork's NEON kernels and runtime dispatch on real Apple silicon.
He is rightly sceptical of running a stranger's code, so the path has to be small,
inspectable, offline and verifiable. The candidate map's RC7 (CPU capability) treats
real Apple silicon reports as extra evidence, and the NEON twins gain from it.

## Decision

We publish one image, `ghcr.io/vmafx/vmafx:<describe>-tester` (`<describe>` is
`git describe --tags --always` of the source commit, which is any commit reachable from
master, or a release tag; a tester image is evidence for that commit, not a product
release; the same rule names the macOS bundle, ADR-1493) (linux/amd64 and
linux/arm64), whose only entry point is `vmaf-tester-report`. It runs offline and prints
one JSON report. The image holds a CPU-only GCC build of the fork in the golden
build profile, 37 SIMD and dispatch unit executables, the Netflix golden pairs and
the Python golden gate. The report code extends `tools/rc1-tester/` (probe, bounded
process runner) and shares its stdlib-only style. Reports a tester wants to credit
are committed as `docs/hardware-reports/<date>-<cpu-slug>.json`, validated by
`scripts/ci/check-hardware-reports.py` (JSON schema, an integrity hash computed by the
tool, "built by the hosted workflow", verdict consistency), with a generated index.

Publishing runs behind the `tester-publish` environment (master only, the maintainer as
required reviewer; `release-publish` is tag-only and guards product releases). Because
that environment admits runs on master only, the image is published by
`workflow_dispatch` on master (input `ref` or `tag`) and no longer on `release: published`,
whose run would sit on the tag ref.

The image tag lives under the already public `ghcr.io/vmafx/vmafx` package, as the
cuda13 and rocm10 variants do, because a new GHCR package starts private and its
visibility cannot be changed through the API. It is built per architecture on native
hosted runners (the repository already uses `ubuntu-26.04-arm`), signed keyless with
cosign and attested like the production images. Dockerfiles for published images other
than `dev/Containerfile` are an existing pattern (`docker/Dockerfile.production`).

The reference scores are produced by the image's own binary at build time. The
tester's scalar run is compared with the baked scalar reference of the same
architecture and the default dispatch with the baked default reference; a default
reference made with different dispatch flags (SVE2 on the build host, none on an M4)
is reported but not gating. The x86_64 scalar scores are carried into the arm64
image as an informational cross-architecture comparison: the fork claims scalar
equals SIMD on one architecture, not x86_64 equals arm64.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Ask the tester to build from a checkout | Nothing prebuilt to trust | Needs a toolchain; the maintainer wants no build | Rejected |
| Reuse `ghcr.io/vmafx/vmafx:<tag>` (distroless CLI) | Already published | No Python, no tests, no fixtures | Rejected |
| New package `vmafx-tester` | Clean name | Starts private, visibility only through the web UI | Rejected: one manual step per release |
| Build the image from `dev/Containerfile` | One Containerfile | GPU SDKs, over 20 GB | Rejected |
| Python golden gate left out | Smaller by about 450 MB (about 0.6 GB on disk) | The golden gate is the numerical ground truth the tester is asked to confirm | Rejected |
| Reports as workflow artifacts or issues only | No tracked files | Not durable, no credit in the history | Offered as the alternative path in the guide |

## Consequences

- **Positive**: one command, no network, a documented hardening line
  (`--network none --read-only --cap-drop ALL --security-opt no-new-privileges
  --tmpfs /tmp`), a signed and attested image, an auditable Containerfile.
- **Negative**: the image is 1.05 GB on disk (`docker image inspect`, linux/amd64) and
  about 270 MB to download, mostly the locked Python test stack (about 330 MB) and the
  fixtures (about 185 MB). The brief's "well under 1 GB" is met for the download, not
  on disk; dropping the Python golden gate would save about 450 MB (left to the
  maintainer). The Debian archive packages of the build stage float, as in the
  release build (ADR-1346).
- **Neutral**: the unit list is `tools/rc1-tester/image/unit-tests.txt`; tests that read
  source-tree files are left out. An image built by hand fails the CI report check
  (`built_by_workflow`).

## References

- `req` (maintainer brief, 2026-10-02, paraphrased): an outside tester with an Apple M4
  must be able to test the fork by running one prepared container image, with no build,
  and be credited for the result; the path must be visibly safe.
- [ADR-1102](1102-phase4b9-container-only-publishing.md),
  [ADR-1346](1346-hosted-slim-container-release-build.md),
  [ADR-1461](1461-strict-fp-every-translation-unit.md),
  [ADR-1317](1317-golden-gate-build-isolation.md).
