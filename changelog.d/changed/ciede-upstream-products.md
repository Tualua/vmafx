- **`ciede2000` follows Netflix's arithmetic again in two products; scores
  move by up to 1.3e-9.** `ciede2000()` multiplies two `float` chromas under
  a square root and three `float` factors in its rotation term. Netflix's
  source forms those products in `float`; since a CodeQL sweep in May 2026
  (PR #552) this fork widened the first operand to `double`, which changed
  the value. The casts are gone
  ([ADR-1476](docs/adr/1476-ciede-upstream-expression.md)), and the CUDA,
  SYCL and HIP twins form the same `float` products. Measured against
  Netflix master (`9e48141b`, GCC 16.2.1, glibc 2.44) at `--precision max`
  on 327 frames from 8x8 to 3840x2160: 153 frames are identical (7 before).
  The rest has recorded causes: 119 frames differ by at most 2.2e-11 because
  the fork squares a `float` by multiplying where upstream calls
  `powf(x, 2)` (ADR-1467; a property of glibc 2.44's `powf`, absent with
  glibc 2.43), 48 are 4:2:2 input, where the fork reads the
  chroma planes with the right subsampling flags, and 7 are odd frame sizes,
  where the fork rounds the chroma size up. What you see: `ciede2000` moves
  on almost every frame by at most 1.3e-9 (1.0e-8 on frames of 24x24 and
  smaller), far below the default `%.6f`. The twins stay within their
  `1e-9` bound: at most 2.1e-12 from the CPU on an RTX 4090, an Arc A380 and
  a gfx1036 (153 frames, 3840x2160 included). The Netflix golden gate is
  unchanged (271 passed, 12 skipped, x86-64 and aarch64).
