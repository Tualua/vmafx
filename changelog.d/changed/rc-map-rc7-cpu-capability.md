- The first-release candidate map has a new RC7, the CPU capability source of
  truth ([ADR-1490](docs/adr/1490-rc3-rc9-candidate-map-cpu-capability.md)): a
  generated, checked-in table of the CPU features each SIMD kernel needs, a
  per-function disassembly audit for x86 and aarch64, and every dispatch level
  run bit-exact against scalar under Intel SDE and qemu. Benchmarks, profiling
  and tuning move from RC7 to RC8 and the one-shot retrain from RC8 to RC9. The
  release guide, roadmap, retrain runbook, tester guide, model card,
  dependency-bot policy, the `docs/state.md` classification and the
  `vmaf-rc1-report` tool inventory use the new numbering.
