- **Every exact GPU twin is measured at 8, 10, 12 and 16 bits and in 4:2:0,
  4:2:2 and 4:4:4.** `scripts/ci/exact_twin_matrix.py` runs each twin declared
  in `scripts/ci/exact_twins.d/` against `--backend cpu` at `--precision max` on
  generated 357x353 fixtures, with no tolerance. `test_cuda_exact_twin_matrix`,
  `test_sycl_exact_twin_matrix` and `test_hip_exact_twin_matrix` run it on a
  device. [The matrix page](docs/development/exact-twin-matrix.md) records the
  result per backend, and `test_exact_twin_matrix_contract` fails while a
  declared CUDA, SYCL or HIP twin has no full, passing row there.
