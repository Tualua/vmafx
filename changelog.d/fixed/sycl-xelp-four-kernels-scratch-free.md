- **No SYCL kernel spills on Intel Xe-LP integrated GPUs any more.** Two
  tester reports from UHD 770 GPUs (issues #2116 and #2122) found 12 SYCL
  kernels using scratch memory, which Intel GPUs under the xe driver can turn
  into wrong values. The `ssimulacra2_sycl` slot kernel, the `ssim_sycl` term
  kernel and the scale-0 SIMD-16 horizontal `vif_sycl` kernel now leave their
  sub-group size to the compiler with the large register file, and the
  16-bit `motion_sycl` SAD kernel runs at SIMD-16: none of the four uses
  scratch memory on any of the 19 default ahead-of-time targets, and every
  twin still returns the CPU's scores bit for bit on an Arc A380. The other
  eight were the SIMD-32 `vif_sycl` kernels: they are removed, with the
  `VMAF_SYCL_VIF_SUBGROUP_SIZE` variable that forced them, and `vif_sycl`
  runs at SIMD-16 on every device
  ([ADR-1830](docs/adr/1830-sycl-vif-simd16-only.md)). SIMD-32 was never
  faster on an A380; setting the variable now has no effect.
