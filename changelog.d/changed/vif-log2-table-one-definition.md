- **The fixed-point VIF log2 table has one definition for every backend.**
  `vif_log2_table_generate()` moved to `core/src/feature/vif_log2_table.h`,
  which `integer_vif.h` includes. The SYCL and Metal hosts of the `vif` twins
  built the same 32768 values with copies of the expression and now call it,
  as the CPU extractor and the HIP host do. No score changes: `vif_sycl`
  stays bit-identical to the CPU on an Arc A380 (48 of 48 and 50 of 50
  frames) and `vif_hip` on a gfx1036; the Metal change is not built or run on
  this host. The table "Which HIP twins return the CPU's bits" on the
  [HIP backend page](docs/backends/hip/overview.md) is re-measured.
