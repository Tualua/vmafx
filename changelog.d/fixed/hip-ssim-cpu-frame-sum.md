- **`integer_ssim_hip` is bit-identical to the CPU `ssim` extractor at every
  frame size.** The CPU adds the SSIM term of every pixel into one `double`,
  left to right and top to bottom. The HIP twin computed the same terms and
  added them in that order only for frames of at most 4096 pixels; above
  that it added them per 16x8 block, and on a gfx1036 the score of 1 of 178
  measured frames was the CPU's, the others up to 1.1e-11 away. The twin now
  stores every term and the host adds the plane in the
  CPU's order: 178 of 178 frames from 480x270 to 3840x2160 at 8 to 16 bits
  are identical at `--precision max`, with `enable_db` and `clip_db` too, and
  an identical frame reports the CPU's value at every size. The parity gate
  compares the CPU and HIP `ssim` cells with tolerance 0. Cost on the
  gfx1036: 30.0 ms instead of 28.2 ms per 1920x1080 frame and 98.1 ms instead
  of 94.3 ms per 3840x2160 frame, and 66 MB more device and pinned host
  memory at 3840x2160. Stored `integer_ssim_hip` scores change by up to
  1.1e-11 ([ADR-1438](docs/adr/1438-hip-ssim-cpu-frame-sum.md),
  [HIP backend](docs/backends/hip/overview.md#integer_ssim_hip)).
