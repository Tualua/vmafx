- **Three GPU helper headers credit the reference code they reproduce.** They
  were created under `EUPL-1.2` and replay arithmetic of an upstream extractor
  for a twin. Each now names `EUPL-1.2 AND` the licences of exactly that code
  and carries its copyright notices above the fork's
  ([ADR-1474](docs/adr/1474-relicense-helper-headers-and-ci-check.md)):
  `metal/float_ms_ssim_option_semantics.h` adds `BSD-2-Clause-Patent`
  (Netflix); `hip/float_ssim/ssim_decimate.h` adds
  `BSD-2-Clause-Patent AND BSD-3-Clause` (Netflix, Tom Distler);
  `sycl/sycl_integer_ssim_math.h` adds `BSD-2-Clause` (Xiph.Org). All paths
  are under `core/src/feature/`. No code line changes.
