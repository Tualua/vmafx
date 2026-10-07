- **SYCL: `libvmaf_sycl` on QSV zero-copy input is 4.5 % faster with
  `vmaf_v0.6.1` on an Arc A380 (43.9 to 45.9 fps at 3840x1600 10-bit), with
  every score bit unchanged.** The scale-0 VIF horizontal pass reads its inputs
  from a local-memory tile, which cuts the filter's GPU time by about 1.2 ms per
  frame. `vmaf_4k_v0.6.1` gains 5.9 %, and a whole 76378-frame episode went
  from 44.99 to 47.44 fps with identical per-frame scores.
  `VMAF_SYCL_TIMING=1` now also prints a `[vmaf-sycl] phases:` line with the
  host milliseconds per frame spent in queue waits and the VA import.
  `scripts/test/zerocopy-throughput.sh` measures the zero-copy path on a real
  pair. The SYCL zero-copy page now covers what limits the frame, start-up for
  short scenes (pass `-an -sn -dn`), and the `n_subsample` and model trade-offs.
  See ADR-1769.
