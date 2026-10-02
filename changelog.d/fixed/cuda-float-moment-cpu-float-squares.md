- **`float_moment_cuda` is bit-identical to the CPU `float_moment` extractor
  at 16 bits.** The CPU forms each sample's square in `float` before adding
  it, which at 16 bits is the square rounded to 24 bits; the CUDA twin added
  exact integer squares. Its second moments (`float_moment_ref2nd`,
  `float_moment_dis2nd`) were up to 1.0e-4 from the CPU's on 16-bit content
  with real low bits (0 of 77 such frames identical on an RTX 4090), while
  8-, 10- and 12-bit input and the first moments were identical. The 16-bit
  kernel now adds the CPU's float square, and all four outputs are identical
  on 262 of 262 measured frames at `--precision max`. The parity gate
  compares the CPU and CUDA `float_moment` cells with tolerance 0. One range
  stays within a derived bound instead: on a 16-bit frame of more than
  2 097 152 pixels whose sum of squares passes 2^53 the CPU's own sum rounds
  as it goes (2.7e-7 measured at 2560x1440). No measurable cost. Stored
  16-bit `float_moment_cuda` second moments change by up to 1.0e-4
  ([ADR-1453](docs/adr/1453-cuda-float-moment-cpu-float-squares.md),
  [CUDA backend](docs/backends/cuda/overview.md#float_moment_cuda-matches-the-cpu-float_moment-at-16-bits-2026-10-02)).
