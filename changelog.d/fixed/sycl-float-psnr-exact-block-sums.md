- **`float_psnr_sycl` is bit-identical to the CPU `float_psnr` extractor at
  every bit depth.** The CPU squares each sample difference in `float` and
  adds the squares in `double`, which does not round. The SYCL twin added
  each 16x16 work-group in single precision: exact at 8 bits, and at 10, 12
  and 16 bits only while the differences in a group are small. On an Arc A380
  it matched the CPU on every frame of real clips and was up to 7.4e-8 dB off
  on high-bit-depth input with large differences (0 of 19 such frames
  identical). The kernel now adds the squares as integers, and 288 of 288
  measured frames are identical at `--precision max`, with `uncapped=true`
  too. The parity gate compares the CPU and SYCL `float_psnr` cells with
  tolerance 0. A 3840x2160 frame takes 3.41 ms instead of 3.31. Stored
  `float_psnr_sycl` scores of such input change by up to 7.4e-8 dB
  ([ADR-1450](docs/adr/1450-sycl-float-psnr-exact-block-sums.md),
  [PSNR](docs/metrics/psnr.md#float_psnr)).
