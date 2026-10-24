<!-- markdownlint-disable MD013 MD060 -->
# ADR-3520: A rootless-Podman dev container built locally from `dev/Containerfile`

- **Status**: Accepted
- **Date**: 2026-10-24
- **Deciders**: maintainer, agent
- **Tags**: dev-container, docker, build, sycl, workspace, fork-local

## Context

The root `.devcontainer/devcontainer.json` is written by praetor and starts
from `ghcr.io/vmafx/vmafx-dev-mcp`. That package stays private: it holds the
full CUDA toolkit, Intel's oneAPI Base Kit and the ROCm payload, whose licences
allow internal use and not redistribution
([ADR-1564](1564-dev-image-private-guard.md)). A contributor outside the
organisation cannot pull it, so the root dev container does not start for them.
Every input of the image is public and pinned, though: the bases in
`build-config.env`, the CUDA and oneAPI apt series, the Intel NEO release and
the digest-pinned ROCm image. A contributor can build the same toolchains on
their own machine, and an image built locally for one's own use is the
internal use those licences allow.

Two Podman behaviours stood in the way of building `dev/Containerfile` with
rootless Podman. Podman's default OCI image format drops the `SHELL`
instruction, so every `RUN` ran under `/bin/sh` without `pipefail`, and the
Intel NEO step failed on its first bash array. Buildah rejects the `env=` key
of a `--mount=type=secret` (`secret should have syntax id=id[,target=path,...]`),
which the NEO step used for the optional GitHub token.

## Decision

We add `.devcontainer/podman/devcontainer.json`, a second dev container
configuration that builds the `gpu-sdks` stage of `dev/Containerfile` on the
contributor's machine with `--format=docker`, runs it under rootless Podman
with the host user mapped to the image's `vmaf` user
(`--userns=keep-id:uid=2000,gid=2000`), passes `/dev/dri` through, and mounts
the host user's Podman socket so `podman` inside reaches the host's engine. The
NEO step of `dev/Containerfile` reads the token from the secret's file mount at
`/run/secrets/github_token`, which BuildKit and Buildah both accept, and
`scripts/ci/check-dev-container-build-secret.py` requires that form. The image
is never pushed; the root configuration stays as praetor writes it.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Root praetor configuration (`ghcr.io/vmafx/vmafx-dev-mcp`) | No local build; praetor engine baked in | Private package (ADR-1564); a contributor outside the organisation cannot pull it | Does not start for the people who need it |
| Build the `dev-mcp` target | Same image as `dev/docker-compose.yml`, `vmaf` and FFmpeg built in | Copies the sources; every commit rebuilds the late stages (an hour and more); the built `vmaf` is stale as soon as the workspace changes | A dev container compiles the workspace itself; baking a build into the image costs time and gives a binary nobody uses |
| Build the `libvmaf-deps` target | Python and codec dependencies present | Also copies the sources, so a commit invalidates its cache | Same cache problem as `dev-mcp` |
| Publish a public image of `gpu-sdks` | No local build | Redistributes the CUDA toolkit and oneAPI, which their licences do not allow | Excluded by ADR-1564 |
| Keep `env=` and require Docker with BuildKit | No change to `dev/Containerfile` | Rootless Podman, the default on Fedora and the host this was built on, cannot build the image | The file-mount form works under both builders for the same lines |

## Consequences

- **Positive**: anyone with rootless Podman and a GPU render node gets the
  CUDA, oneAPI (`icpx`), Intel NEO / Level Zero, `ocloc` and ROCm toolchains
  in VS Code without registry access, and the SYCL backend runs on the host's
  Intel GPU from inside. Measured on an Arc Pro B50 (Xe2, `xe` driver):
  `sycl-ls` lists it through Level Zero and OpenCL, `test_sycl_exact_twins`
  and the SYCL parity tests pass, and `vmaf --backend sycl` on the 576x324
  Netflix pair returns the CPU's pooled VMAF (82.81606). Files written in the
  workspace stay the host user's.
- **Negative**: the first build pulls the ROCm image (about 21 GB); on the
  host it was measured on (Xeon D-2143IT, 16 threads) it took 86 minutes and
  the image is 28.1 GB. CUDA and HIP compile but run only where the host has
  the hardware and passes it through (`--device=/dev/dri` covers Intel and the
  AMD render node, not `/dev/kfd` or NVIDIA). The configuration does not carry
  the praetor engine; `.config/lefthook/engine.sh` installs it with
  `go install` on first use, which the Go feature provides.
- **Neutral / follow-ups**: the three other `env=GITHUB_TOKEN` secret mounts
  (`docker/Dockerfile.production-gpu`, `docker/Dockerfile.tester`) are built
  by CI with BuildKit and keep their form. A change to the `gpu-sdks` stage
  reaches this configuration on the next "Rebuild Container".

## References

- [ADR-1564](1564-dev-image-private-guard.md): why the dev image is private.
- [ADR-0451](0451-local-dev-mcp-container.md): the dev-MCP container.
- User guide: [IDE setup, Podman dev container](../development/ide-setup.md#podman-dev-container).
- Source: `req` — "Насколько я помню, мы не можем использовать апстримный контейнер, т.к. он использует приватный образ и должны собирать свой"; "Давай, сделай по возможности все сам. Имей ввиду, GPU обновлен до B50".
