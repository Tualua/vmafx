- **`ssimulacra2_sycl` is bit-identical to the CPU `ssimulacra2` extractor.**
  The CPU evaluates six terms per pixel and channel in `double` and adds each
  into one `double`, pixel after pixel. A SYCL device has no `double`, so the
  twin evaluated the terms as pairs of floats and added them in a fixed tree:
  on an Arc A380 none of 266 measured frames (576x324 to 3840x2160, 8 to 16
  bits) equalled the CPU, and the score was up to 7.6e-11 away. The twin now
  computes each term's `double` in 64-bit integers, the CPU's operations one
  for one, and forms the sums with the bits of the CPU's loops, from integer
  increments per binade as the CUDA and HIP twins do (`ordered_sum.h`). All
  266 frames are identical at `--precision max`, with every `yuv_matrix`. The
  parity gate compares the CPU and SYCL `ssimulacra2` cells with tolerance 0
  instead of 5e-3, and the Arc A380's 5e-2 calibration for this feature is
  removed. The twin is slower: 195 ms instead of 84 per 3840x2160 frame and
  13.5 ms instead of 5.3 per 576x324 frame on that device. Stored
  `ssimulacra2_sycl` scores change by up to 7.6e-11
  ([ADR-1446](docs/adr/1446-sycl-ssimulacra2-cpu-bits.md),
  [ssimulacra2](docs/metrics/ssimulacra2.md#sycl-device-resident-one-readback-per-frame)).
