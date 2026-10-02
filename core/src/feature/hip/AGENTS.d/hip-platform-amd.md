---
paths:
  - core/src/meson.build
  - core/meson.build
invariant: HIP_PLATFORM_AMD macro definition originates solely from the build system.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# `__HIP_PLATFORM_AMD__` comes from the build (ADR-1263)

New HIP host source needs **no** `#define __HIP_PLATFORM_AMD__`. `hip_deps` in
`core/src/hip/meson.build` supplies `-D__HIP_PLATFORM_AMD__=1` to every HIP TU,
outside the `hip_runtime_dep` discovery branch, so both branches get it.

Copying the old `#define` from a neighbour re-adds a reserved identifier
(`cert-dcl37-c`): next PR touching that file then owns removing it.
