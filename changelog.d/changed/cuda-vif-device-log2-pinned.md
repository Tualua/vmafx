- **`vif_cuda` is declared bit-identical to the CPU `vif` extractor, with a
  proof that covers every input.** The CPU reads its logarithms from a table
  of 32768 values built with the host math library; `vif_cuda` computes them
  on the device, which on an AMD GPU had moved 77 of the values (ADR-1435). A
  new test launches a probe kernel and compares the device's value with the
  CPU's table for all 32768 entries: all are equal on an RTX 4090 (CUDA 13.4,
  glibc 2.44), although the device's `log2f()` differs from the host's by one
  unit in the last place for 307 arguments. The parity gate now compares the
  CPU and CUDA `vif` cells with tolerance 0; 1392 of 1392 scores on 348
  frames are identical at `--precision max`. No scoring kernel, stored score
  or frame time changes. If the test fails on another host or CUDA release,
  the twin has to read the CPU's table as the HIP twin does
  ([ADR-1456](docs/adr/1456-cuda-vif-device-log2-pinned.md),
  [CUDA backend](docs/backends/cuda/overview.md#vif_cuda-returns-the-cpus-scores-bit-for-bit-2026-10-02)).
