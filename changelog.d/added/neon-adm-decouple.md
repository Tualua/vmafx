- **`adm` has a NEON scale-zero decouple on aarch64.** `adm_decouple_neon()`
  (`core/src/feature/arm64/adm_neon.c`, ported from Netflix/vmaf `9e48141b`)
  decouples four columns at a time for integral enhancement gain limits and
  hands fractional limits to the scalar kernel, so it returns the scalar
  kernel's bits at every `adm_enhn_gain_limit` from 1 to 100. No score
  changes; only the time of the scale-zero decouple does. `test_integer_adm_simd`
  now runs on aarch64 and holds the kernel to the scalar one at gain limits
  1, 1.2, 1.5, 2, 3, 7 and 100.
