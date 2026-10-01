- **`vif_hip` is bit-identical to the CPU `vif` extractor.** The fixed-point
  VIF statistic takes every per-pixel logarithm from a 32768-entry table the
  CPU extractor fills with the host math library. The HIP twin evaluated
  `log2f()` on the device instead, which is one ulp from glibc's for about
  half of the arguments and rounded ties to even where the CPU rounds them
  away from zero: 77 entries were one lower, and on a gfx1036 only 49 of 440
  scores (four scales, 110 frames from 480x270 to 3840x2160) were the CPU's,
  the others up to 5.4e-7 away. The twin now uploads the CPU's table and
  looks every logarithm up; all 440 scores are identical at `--precision max`,
  and so are the numerator and denominator sums of `debug=true`, 12- and
  16-bit and 4:2:2 input, `vif_enhn_gain_limit=1.0` and `vif_skip_scale0`.
  The table has one definition, `vif_log2_table_generate()` in
  `integer_vif.h`. The parity gate compares the CPU and HIP `vif` cells with
  tolerance 0. Stored `vif_hip` scores change by up to 5.4e-7
  ([ADR-1435](docs/adr/1435-hip-vif-cpu-log2-table.md),
  [HIP backend](docs/backends/hip/overview.md#vif_hip-returns-the-cpus-scores-bit-for-bit-2026-10-01)).
