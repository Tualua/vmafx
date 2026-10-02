- **`vif_cuda` reads the CPU's log2 table instead of computing logarithms on
  the device.** The fixed-point `vif` takes its logarithms from a table the
  host math library fills. The CUDA twin evaluated `log2f()` on the device,
  which gave the same table on an RTX 4090 with CUDA 13.4 and glibc 2.44 but
  would not have to with another math library or CUDA release (on an AMD GPU
  the same construction was wrong on 77 of 32768 values). The twin now
  uploads the CPU's table when it starts and looks every logarithm up, as the
  HIP, SYCL and Metal twins do, so it returns the CPU's scores by
  construction. No score changes on the measured host (1392 of 1392 scores
  on 348 frames identical at `--precision max`, before and after) and the
  frame time is unchanged
  ([ADR-1462](docs/adr/1462-cuda-vif-reads-host-log2-table.md),
  [CUDA backend](docs/backends/cuda/overview.md#vif_cuda-returns-the-cpus-scores-bit-for-bit-2026-10-02)).
