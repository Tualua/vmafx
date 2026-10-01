- **`float_moment_hip` and `vif_hip` no longer return a wrong first frame in
  a later, larger context of one process.** Both cleared their device
  accumulators ahead of the frame's plane upload. On a gfx1036 such a clear
  has no effect in the first context of a process that needs larger planes
  than the contexts before it, so the frame's sums were added onto the sums
  the earlier context had left in recycled device memory: after a 640x360
  context, the first frame of a 3840x2160 context had `float_moment_ref1st`
  130.53 where the CPU has 127.00, and `vif_hip` scale 0 at 0.6748 where the
  CPU has 0.6934. Every HIP extractor now uploads, then clears, then launches
  its kernels (`float_psnr_hip` is reordered too; its scores were right).
  The `vmaf` tool creates one context per process and was not affected;
  programs that score several clips through the library were. A device test
  per extractor and a source check over every HIP file hold the order
  ([ADR-1427](docs/adr/1427-hip-clear-after-upload.md),
  [HIP backend](docs/backends/hip/overview.md#a-frame-clears-its-accumulators-after-its-upload-adr-1427)).
