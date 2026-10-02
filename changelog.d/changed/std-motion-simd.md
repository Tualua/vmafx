- **The integer motion SIMD kernels meet the lint and HISS standard.**
  `core/src/feature/x86/motion_avx2.c`, `core/src/feature/x86/motion_avx512.c`
  and `core/src/feature/arm64/motion_neon.c` have no clang-tidy finding in any
  lane (7, 18 and 2 before) and no function over 60 lines (ten before): each
  pipeline is now a row loop over small inlined stages. No score changes: the
  old and the new kernels return the same bits on 286 952 generated cases with
  GCC, clang and icx, and `motion`, `motion_v2` and the default model are
  identical at `--precision max` under scalar, AVX2, AVX-512 and NEON dispatch.
  The three files carry their `SPDX-License-Identifier` line
  ([ADR-1142](docs/adr/1142-whole-codebase-standards.md),
  [ADR-1250](docs/adr/1250-eupl-fork-relicense.md)).
