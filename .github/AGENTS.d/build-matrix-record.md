---
paths:
  - .github/workflows/libvmaf-build-matrix.yml
  - .github/workflows/build.yml
invariant: Matrix lanes governed by ADR-1259; experimental lanes advisory; pass -Db_lto=false on hosted icpx/SYCL builds.
---
# CI build matrix of record and compiler configuration (ADR-1259)

## Build matrix of record (ADR-1259)

[ADR-1259](../../docs/adr/1259-ci-build-matrix-as-it-runs.md) lists every lane in
`libvmaf-build-matrix.yml` and `build.yml` and which ones are required.
ADR-0689, ADR-0691, ADR-0710 and ADR-0728 are superseded: do not remove lane
on their authority, and do not let merge resolution drop or restore lane
without ADR. That is how `384d97d03` undid two of them.

MoltenVK lane (ADR-0338) went with Vulkan backend (ADR-0726).
`libvmaf-build` job's `continue-on-error` is now
`${{ matrix.experimental == true }}`, so two `experimental: true` rows,
`macOS clang` and `macOS clang+DNN`, are advisory: their failure does not
fail workflow run. Neither is required check.

## Related

- [ADR-0124](../../docs/adr/0124-automated-rule-enforcement.md) — this tooling
- [ADR-1247](../../docs/adr/1247-scorecard-exact-head-gates.md) — current OSSF
  Scorecard policy; ADR-0263 is superseded
- [ADR-0338](../../docs/adr/0338-macos-vulkan-via-moltenvk-lane.md) — macOS
  Vulkan-via-MoltenVK advisory lane (removed with Vulkan backend, ADR-0726)
- [ADR-1259](../../docs/adr/1259-ci-build-matrix-as-it-runs.md) — CI build
  matrix as it runs
- [Research-0002](../../docs/research/0002-automated-rule-enforcement.md) — investigation
- [Research-0053](../../docs/research/0053-ossf-scorecard-investigation.md) —
  OSSF Scorecard per-check breakdown
- [Research-0089](../../docs/research/0089-moltenvk-feasibility-on-fork-shaders.md)
  — MoltenVK feasibility against fork's shader inventory
- [`docs/development/automated-rule-enforcement.md`](../../docs/development/automated-rule-enforcement.md)
  — user-facing explainer
- [`docs/rebase-notes.md` entry 0026](../../docs/rebase-notes.md) — sync ledger
- [ADR-0363](../../docs/adr/0363-renovate-replaces-dependabot.md) —
  Renovate replaces Dependabot
- [`docs/development/dependency-bot.md`](../../docs/development/dependency-bot.md)
  — operator playbook

## SYCL build trees on hosted runners must disable LTO

`core/meson.build` sets `b_lto=true` in project's `default_options`, so any
`meson setup` not overriding it links with `-flto`. On
GitHub-hosted `ubuntu-24.04` runner, this routes LTO through stock binutils
LLVM gold plugin, which is **LLVM 17.0.6**. It cannot read bitcode emitted by
oneAPI DPC++ compiler, and every binary fails to link:

```text
bfd plugin: LLVM gold plugin has failed to create LTO module:
Unknown attribute kind (102)
(Producer: 'Intel.oneAPI.DPCPP.Compiler_2026.1.1' Reader: 'LLVM 17.0.6')
```

Pass `-Db_lto=false` on every icpx/SYCL `meson setup` in CI. Both SYCL legs of
`libvmaf-build-matrix.yml` already do, as do `build.yml`'s `Linux Intel LLVM`
row and `Tidy SYCL` job. Pinning older oneAPI does not
help — mismatch is against *system* linker plugin, not specific
compiler release.
