- **`psnr_hvs` returns Netflix's values again; scores move by up to 9.4e-7 dB
  on some frames.** The masking threshold of `psnr_hvs` is the square root of
  a product of two `float` values. Netflix's source forms that product in
  `float`; since a CodeQL sweep in May 2026 (PR #552) this fork widened it to
  `double`, which put the threshold one `float` step off on about one block
  in twenty. The cast is gone
  ([ADR-1488](docs/adr/1488-psnr-hvs-upstream-mask-product.md)). Measured
  against Netflix master (`9e48141b`, GCC 16.2.1, glibc 2.44) at
  `--precision max` on 319 frames from 8x8 to 3840x2160 at 8 to 12 bits:
  `psnr_hvs`, `psnr_hvs_y`, `psnr_hvs_cb` and `psnr_hvs_cr` are identical on
  every frame, scalar, AVX2 and default dispatch (before: 292, 310, 310 and
  309 of 319). What you see: on 27 of those frames a score moves by at most
  9.4e-7 dB, so a value printed with the default `%.6f` can change in its
  last digit. The AVX2 and NEON functions and the CUDA, HIP and SYCL twins
  return the new value bit for bit: 612 of 612 values identical to the CPU on
  an RTX 4090, a gfx1036 and an Arc A380 (3840x2160 included), and the
  parity gate still compares the three twins with tolerance 0. The SYCL twin
  takes a correctly rounded `float` root of the `float` product, which equals
  the CPU's `double` root rounded to `float` for every product; its integer
  square root (`sqrt_prod_rn()`) is removed. The Netflix golden gate is
  unchanged (271 passed, 12 skipped, x86-64 and aarch64).
