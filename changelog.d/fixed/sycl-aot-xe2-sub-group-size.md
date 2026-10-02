- **The default SYCL build compiles for Lunar Lake and Arc B-series again.**
  Six kernels required a sub-group size of 8, which Xe2 GPUs (`lnl-m`,
  `bmg-g21`, `bmg-g31`) do not offer, so the ahead-of-time compile of
  `float_motion_sycl`, `float_adm_sycl`, `float_vif_sycl` and
  `ssimulacra2_sycl` failed for those targets ("Kernel compiled with required
  subgroup size 8, which is unsupported on this platform") and the dev
  container image did not build; a build without ahead-of-time targets had
  the same failure on such a device at run time. The kernels require 16 now,
  and the build rejects any size but 16 or 32. On an Arc A380 the scores are
  bit-identical and a 4K frame takes at most 2 % longer. Not yet measured on
  an Xe2 device. `meson test --suite sycl-aot` compiles every SYCL source
  file for all default targets from any build
  ([SYCL backend](docs/backends/sycl/overview.md#sub-group-sizes-and-the-aot-targets-adr-1468),
  [ADR-1468](docs/adr/1468-sycl-sub-group-sizes-every-aot-target.md)).
