- **The parity gate bounds `speed_chroma` between the CPU and the HIP twin at
  `5e-6` instead of `5e-5`** (ADR-1452). `speed_chroma_hip` rounds `log2`
  correctly and `speed.c` calls the C library's `log2f`; on a gfx1036 13 of
  990 values differ from a glibc CPU, by 1.4e-6 at most, and none with a
  correctly rounded `log2f` preloaded, the CUDA twin's figures (ADR-1430). No
  score changes. `test_hip_speed_chroma_parity` now compares all three scores
  of every frame on a fixture that reaches the scoring path.
