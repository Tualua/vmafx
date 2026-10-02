---
paths:
  - dev/Containerfile
  - scripts/ci/install-cuda-toolkit.sh
  - build-config.env
invariant: Install CUDA via installer script; use unversioned intel-basekit; install rocm-hip-runtime-dev without rccl.
---
<!-- markdownlint-disable MD013 -->
# GPU SDK package names in Containerfile

## CUDA package names

- Use `scripts/ci/install-cuda-toolkit.sh --mode=full`; do not recreate apt
  bootstrap or install bare `cuda-toolkit-<series>` here. `build-config.env`
  owns `CUDA_APT_PACKAGE`, `CUDA_APT_LOCK_RELEASE`, and exact toolkit,
  nvcc, and cudart Debian versions (ADR-1285 / ADR-1306). installer passes
  `package=version` and verifies installed dpkg values.
- Do NOT install `libcuda1` (runtime driver) — must come from
  `nvidia-container-runtime` at run-time; baking it in shadows host
  driver.
- Do NOT install `cuda-compiler` — legacy alias no longer existing in
  NVIDIA CUDA channels; full mode installs exact toolkit and nvcc packages.

## Intel oneAPI package name

- Use `intel-basekit` (unversioned meta-package).
- Do NOT use `intel-basekit-<year>.<quarter>` (e.g.,
  `intel-basekit-2025.3`): Intel doesn't publish
  year-quarter-versioned meta-package names in
  `apt.repos.intel.com/oneapi`. Versioned name causes
  `E: Unable to locate package`.

## ROCm / HIP package names

- Use `rocm-hip-runtime-dev` (not `rocm-hip-sdk`).
- `rocm-hip-sdk` transitively installs `rccl` (multi-GPU
  collectives), depends on `libdrm-amdgpu-amdgpu1` +
  `libdrm2-amdgpu` — packages absent from ROCm noble apt repo.
  libvmaf HIP kernels use one GPU per worker; rccl not needed.
