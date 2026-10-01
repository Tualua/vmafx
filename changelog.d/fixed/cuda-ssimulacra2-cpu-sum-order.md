- **`ssimulacra2_cuda` returns the CPU extractor's score bit for bit.** The
  CUDA twin of `ssimulacra2` computed the CPU's per-pixel terms but added
  them in a tree, where the CPU adds them one after the other into one
  `double`; every add rounds, so the two ended a few units in the last place
  apart. Measured on an RTX 4090 at `--precision max`, 8 of 113 frames
  matched and the rest were up to 7.3e-11 away. The twin now forms the sums
  of the CPU's loops on the device
  ([ADR-1433](docs/adr/1433-cuda-ssimulacra2-cpu-sum-order.md)): while a
  running sum stays between two powers of two, adding a term moves it by a
  whole number of steps, so the device adds those whole numbers per
  1024-pixel chunk in parallel, one pass over the chunks puts them together,
  and the few chunks in which the sum passes a power of two are added term by
  term. All 113 frames are identical (Netflix 576x324 at 8, 10, 12 and 16
  bits, both 1080p checkerboard pairs, BBB 3840x2160), and the parity gate
  compares the cell at 0 instead of `5e-3`. The price is time: a 3840x2160
  frame takes 15.6 ms instead of 7.8 ms and a 576x324 frame 1.7 ms instead of
  0.4 ms; the CPU extractor takes 126 ms per 4K frame on sixteen threads.
  Stored `ssimulacra2_cuda` scores change by up to 7.3e-11. The SYCL and HIP
  twins keep their tree sums and the `5e-3` tolerance.
