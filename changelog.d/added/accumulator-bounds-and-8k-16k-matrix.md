- **Integer accumulator bounds and an exactness matrix at 8K and 16K.**
  `docs/development/accumulator-bounds.md` gives every integer that sums,
  counts or indexes in a CPU extractor, SIMD path or GPU twin a derived bound
  at 8K DCI, 16K and the 32768 picture cap, with 16-bit 4:4:4 worst-case
  content. `scripts/ci/exact_twin_matrix.py --grid 8k` compares every exact
  CUDA, SYCL and HIP twin with the CPU at 8192x4320 4:4:4 (8 and 16 bit), and
  `--grid 16k` compares the CPU's SIMD and scalar code at 15360x8640; the
  results are recorded in `docs/development/exact-twin-matrix.md`.
