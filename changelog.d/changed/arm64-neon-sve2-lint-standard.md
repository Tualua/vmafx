- **The aarch64 NEON / SVE2 sources and their tests are at the lint and HISS standard (ADR-1142).**
  `core/src/feature/arm64/{convolve_neon,ssimulacra2_neon,ssimulacra2_sve2,ssimulacra2_host_neon}.c`,
  `ms_ssim_decimate_neon.h`, `core/src/arm/cpu.h`, `core/src/feature/simd_dx.h` and
  the NEON tests under `core/test/` report no clang-tidy finding on the arm64
  lane, and the eleven SSIMULACRA 2 NEON / SVE2 kernels that exceeded 60 lines
  are split into helpers. The scalar parts the NEON, SVE2 and host files each
  carried (XYB, the SSIM and edge-difference sums, the YUV conversion) now live
  once in `core/src/feature/arm64/ssimulacra2_arm64_common.h`. Every operation
  runs in the same order: the NEON and SVE2 parity tests and the aarch64
  Netflix golden gate pass under qemu-user, so no score changes.
