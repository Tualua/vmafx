- **The parity gate covers `speed_chroma` on CUDA, at `5e-6`.**
  `speed_chroma_cuda` reproduces the CPU extractor's arithmetic and rounds
  `log2` correctly; the CPU extractor calls the C library's `log2f`, and
  glibc's is the neighbouring `float` for up to 1 % of its arguments.
  Measured on an RTX 4090 at `--precision max` (Netflix 576x324 at 8, 10, 12
  and 16 bits, both 1080p checkerboard pairs, 200 frames of BBB 3840x2160):
  776 of 789 values are identical to the CPU, the other 13 differ by one to
  five steps of the 32-bit score (1.4e-6 at most), and all 789 are identical
  when the CPU run uses a correctly rounded `log2f`
  ([ADR-1430](docs/adr/1430-cuda-speed-chroma-log2f-bound.md)). The twin and
  its scores do not change. The gate had no `speed_chroma` cell before; the
  CUDA parity test compared one score of one frame at `1e-4` on a fixture
  that never reached the scoring path, and now compares all three scores of
  every frame to one part in a million on one that does.
