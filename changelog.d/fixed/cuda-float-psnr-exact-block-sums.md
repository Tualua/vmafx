- **`float_psnr_cuda` is bit-identical to the CPU `float_psnr` extractor at
  every bit depth.** The CPU squares each sample difference in `float` and
  adds the squares in `double`, which does not round. The CUDA twin added
  each 16x16 block in single precision: exact at 8 bits, and at 10, 12 and
  16 bits only while the differences in a block are small. On an RTX 4090 it
  matched the CPU on every frame of real clips and was up to 1.2e-7 dB off
  on high-bit-depth input with large differences (2 of 92 such frames
  identical). The kernel now adds the squares as integers, and 268 of 268
  measured frames are identical at `--precision max`, with `uncapped=true`
  too. The parity gate compares the CPU and CUDA `float_psnr` cells with
  tolerance 0. No measurable cost. Stored `float_psnr_cuda` scores of such
  input change by up to 1.2e-7 dB
  ([ADR-1455](docs/adr/1455-cuda-float-psnr-exact-block-sums.md),
  [PSNR](docs/metrics/psnr.md#float_psnr)).
