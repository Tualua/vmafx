- **The CUDA, SYCL and HIP tester images build again.** The exactness-matrix
  test names `scripts/ci/exact_twin_matrix.py`, which `meson setup` resolves
  for every enabled GPU backend, but the tester image's GPU build stages did
  not copy it, so every GPU image failed at configure time. The four Meson
  stages of `docker/Dockerfile.tester` now copy it, and a test refuses any
  file the Meson tree names outside `core/` that one of those stages lacks.
