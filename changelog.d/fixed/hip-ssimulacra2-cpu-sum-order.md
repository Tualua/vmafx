- **`ssimulacra2_hip` is bit-identical to the CPU `ssimulacra2` extractor.**
  The CPU evaluates six terms per pixel and channel in `double` and adds each
  into one `double`, pixel after pixel. The HIP twin evaluated the terms as
  pairs of floats and added them in a fixed tree: on a gfx1036 none of 178
  measured frames (480x270 to 3840x2160, 8 to 16 bits) equalled the CPU, and
  the score was up to 7.6e-11 away. The twin now evaluates the CPU's double
  expressions and forms the sums with the bits of the CPU's loops, from
  integer increments per binade as the CUDA twin does (`ordered_sum.h`). All
  178 frames are identical at `--precision max`, with every `yuv_matrix`. The
  parity gate compares the CPU and HIP `ssimulacra2` cells with tolerance 0
  instead of 5e-3. The twin is slower: 167 ms instead of 58 per 1920x1080
  frame and 662 ms instead of 234 per 3840x2160 frame on that device. Stored
  `ssimulacra2_hip` scores change by up to 7.6e-11
  ([ADR-1445](docs/adr/1445-hip-ssimulacra2-cpu-sum-order.md),
  [ssimulacra2](docs/metrics/ssimulacra2.md#hip-device-resident-tiled-row-pass)).
