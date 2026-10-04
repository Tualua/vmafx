- Corrected the contributor pages against the build, the workflows and the
  scripts. `build-flags.md` documents every Meson option with the project
  defaults (`buildtype=release`, `default_library=both`); `oneapi-install.md`
  gains a "Building with icx / icpx" section (glibc math since ADR-1495, strict
  FP, golden gate on gcc or clang); `cross-backend-gate.md` lists Metal as a
  gate backend and explains exact twins for users; `release.md` matches the
  release-please configuration and the required checks; `ci.md` is split into
  shorter pages. Features that are not wired (eBPF FUSE bypass, perf regression
  gate) are described as such.
