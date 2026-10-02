- **The HIP runtime and four HIP host files are clean under clang-tidy
  (ADR-1142).** `core/src/hip/kernel_template.c` and `core/src/hip/common.c`
  convert the stream and event handles they keep as `uintptr_t` through
  `hip_handle.h` instead of integer-to-pointer casts; `core/src/hip/stubs.c`
  carries the ADR-1138 `NULL` bracket; `speed_chroma_hip.c`,
  `speed_temporal_hip.c`, `float_adm_hip.c` and `hip_hsaco_stubs.c` lose
  their remaining findings. The `hip` lane baseline drops from 752 to 733
  and the `cpu`, `cuda`, `sycl` and `arm64` lanes by 2 each. No behaviour
  change: every HIP twin returns the same values as before on a gfx1036
  (17 800 of 17 800 values of the sweep).
