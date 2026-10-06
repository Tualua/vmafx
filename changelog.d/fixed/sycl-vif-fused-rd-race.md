- **`vif_sycl` with `vif_fused=true` returns the CPU's scores.** From 1920x1080
  up, scales 1 to 3 differed from the CPU `vif` on every frame (by up to 4.9e-4
  at 3840x2160): one fused launch read a scale from the downsampled buffers it
  was writing the next scale into. The fused scales now alternate between two
  buffers, which costs 4 MB of device memory at 3840x2160; the default separate
  passes were not affected.
