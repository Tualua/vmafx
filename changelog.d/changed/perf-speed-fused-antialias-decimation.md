- **SpEED filters only the samples it keeps on non-x86 targets (port of
  Netflix/vmaf `76ea5f03`, [Netflix/vmaf#1653](https://github.com/Netflix/vmaf/pull/1653)).**
  `speed_chroma` and `speed_temporal` blur each plane with a Gaussian
  anti-alias filter and keep one sample in 256. On aarch64 and every other
  target without the AVX2 convolution the extractor now evaluates the filter
  at the kept samples only (`vif_filter1d_dec16_s()`): the vertical pass runs
  for one row in 16 and the horizontal pass for one column in 16 of it. The
  result has the bits of the filter-then-decimate path, so no score changes
  (`core/test/test_speed_filter.c`; before/after reports under `qemu-aarch64`
  are byte-identical). x86 is unchanged. The NEON covariance kernel of the same
  upstream pull request (`15297286`) is not taken: its partial sums are not
  bit-identical to the scalar kernel; `docs/rebase-notes.md` has the numbers.
  `docs/backends/arm/overview.md` showed `--cpumask 0` as the scalar-only
  switch; `--cpumask` takes the bits to mask out, so that is `--cpumask 3` on
  aarch64 (corrected).
