---
paths:
  - dev/Containerfile
  - build-config.env
invariant: Release bundle compiles in Debian 13 release-build; never base on Ubuntu; xxd and patchelf pinned.
---
<!-- markdownlint-disable MD013 -->
# `release-build` stage (ADR-1354)

Native release bundle compiles in `release-build`: separate root
`FROM ${RELEASE_BUILDER_BASE}` (Debian 13, glibc 2.41), placed before
`gpu-sdks` so last stage stays `dev-mcp`. Never base it on, or copy from,
Ubuntu stage: bundle would bind glibc 2.43 again. Marker write must
stay byte-identical to `build-deps` one (unit suite requires exactly two
writes). `xxd` load-bearing (built-in model embed); `patchelf` pinned to
Debian 13 package (`PATCHELF_VERSION`, RUNPATH fix). Moving
`RELEASE_BUILDER_BASE` to another Debian release fails that install until
pin follows.
