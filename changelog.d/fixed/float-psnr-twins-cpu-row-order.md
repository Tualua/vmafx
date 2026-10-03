- **`float_psnr_cuda`, `float_psnr_sycl` and `float_psnr_hip` are
  bit-identical to the CPU `float_psnr` on every frame, 16-bit frames whose
  sum of squared differences passes 2^53 units included.** The CPU adds each
  row exactly and the rows into one `double`, which rounds there; the twins
  rounded the exact frame total once and were up to 3.3e-13 dB from the CPU
  (0 of 8 frames of 16-bit 3840x2160 half-range noise identical on CUDA and
  HIP, 1 of 8 on SYCL). Each kernel block now covers 256 pixels of one row,
  and the host adds each row's exact sum in the CPU's order
  (`core/src/feature/float_psnr_rows.h`). No measurable cost
  ([ADR-1499](docs/adr/1499-float-psnr-twins-cpu-row-order.md),
  [PSNR page](docs/metrics/psnr.md#past-253-units-2026-10-03)).
