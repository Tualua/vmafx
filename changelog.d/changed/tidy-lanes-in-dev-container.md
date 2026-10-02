- **The clang-tidy lanes are measured in the dev container.**
  `make tidy-lane LANE=<cpu|cuda|hip|sycl|arm64|all>` (`scripts/dev/tidy-lane.sh`)
  copies a checkout into a throwaway container of the dev image, configures
  the lane with its real toolchain and runs the ratchet;
  `make tidy-lane-write` rewrites the lane's baseline
  ([ADR-1471](docs/adr/1471-tidy-lanes-in-dev-container.md),
  [measuring the clang-tidy lanes](docs/development/tidy-lanes.md)). The
  `cpu` baseline, written on a workstation before, failed the required
  `Tidy Ratchet` check on the first complete hosted run of master (24 files
  below it); the container reproduces the hosted report byte for byte. The
  `cuda`, `hip` and `sycl` lanes build with nvcc, hipcc and icpx, so they
  parse the device bodies and, for the first time, the kernels:
  `scripts/ci/gen-gpu-compile-commands.py` had found no `.cu` / `.hip` rule
  since the kernel targets list their headers, and now stops the lane when
  it cannot read one. The `arm64` cross lane runs in the same container. The
  five baselines are re-measured; the findings in the kernels (473 in the
  CUDA lane, 314 in the HIP lane) are recorded, not yet fixed. A check or a
  scoped write under a clang-tidy other than the baseline's stops with exit 5.
