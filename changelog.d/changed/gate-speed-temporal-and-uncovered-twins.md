- **The cross-backend parity gate covers every registered CUDA, SYCL and HIP
  twin.** `speed_temporal` was the one feature whose three GPU twins were
  registered and compared by no gate cell. It is now a gate feature, with a
  derived bound of `4e-5` (five float steps of a score below 128): measured
  at `--precision max` on an RTX 4090, a gfx1036 and an Arc A380, the twins
  return the CPU's value on every frame except 2 of 104 frames of BBB
  3840x2160, where glibc's `log2f` is not correctly rounded and the CPU is
  one float step (4.8e-7) away. The `psnr` cell now compares `psnr_cb` and
  `psnr_cr` as well as `psnr_y`, and the single-feature gate
  (`cross_backend_vif_diff.py`) gained `ssim`. A new test fails when a
  registered twin has no gate feature. The Metal twins stay outside the gate,
  which has no `metal` backend
  ([ADR-1460](docs/adr/1460-gate-speed-temporal-and-uncovered-twins.md),
  [gate guide](docs/development/cross-backend-gate.md)).
