<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1471: The clang-tidy lanes are measured in the dev container, with the device toolchains

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `ci`, `lint`, `build`, `rc3`, `fork-local`

## Context

The whole-tree clang-tidy ratchet ([ADR-1142](1142-whole-codebase-standards.md))
holds one count per file and lane in `scripts/ci/tidy-baseline-<lane>.json`.
A count depends on the C library, the compilers and the device toolchains of
the machine that measured it. Three things had drifted apart:

- **The `cpu` baseline was written on a workstation.** It named `cc (GCC)
  16.2.1` (glibc 2.44) as its compiler; the required hosted job `Tidy Ratchet`
  measures with gcc-15 on Ubuntu 26.04 (glibc 2.43). Cleanups then tightened
  single translation units with the scoped write of
  [ADR-1243](1243-tidy-scoped-baseline-tightening.md), which cannot lower a
  header's count. The first complete hosted run after two days of landing
  through the local merge train (master `513d2a6fc`, run 37011276599) failed:
  24 files below their baseline, 322 findings measured against 376 recorded.
- **The `hip` lane linted stubs.** It was configured `-Denable_hipcc=false`,
  where 21 HIP host files compile to `-ENOSYS` stubs
  (`T-TIDY-RATCHET-GPU-LANES-UNREPRODUCIBLE-2026-09-22`).
- **No lane measured a kernel.** `scripts/ci/gen-gpu-compile-commands.py`
  matched `<kernel> | <compiler>` in `build.ninja`. Since the kernel targets
  list their headers as dependencies the line reads
  `<kernel> | <headers...> <compiler>`, the generator found no rule, and the
  `cuda` and `hip` baselines held no `.cu` / `.hip` file at all. The `sycl`
  lane could not be measured in full either: 60 of its 408 translation units
  drew a driver warning without a source location from stock clang, which the
  ratchet counts as a failed translation unit
  (`T-SYCL-TIDY-OVERRIDING-OPTION-2026-10-02`, fixed by #1867 while this
  decision was being measured).

- **The workstation's clang-tidy is not pinned.** A package upgrade moved it
  from 22.1.8 to 23.1.1 on 2026-10-02. From then a lane measured on the host
  reports with a version no baseline names, and the scoped write refuses. The
  `arm64` cross lane ([ADR-1283](1283-whole-tree-ratchet-arm64-lane.md)) was
  measured only there.

The hosted run also showed `a.c: warnings 3 -> 5 (+2)` as an error of the
lane. `a.c` is the fixture of the ratchet's own unit test: the job runs that
test first, the test called `report()` without capturing its output, and
under GitHub Actions `report()` prints workflow commands, so the fixture
became an annotation on the run.

## Decision

The dev container (`dev/Containerfile`: Ubuntu 26.04, gcc-15, CUDA with nvcc,
ROCm with hipcc, oneAPI with icpx, ONNX Runtime) is the one place the
clang-tidy lanes are measured and their baselines written.
`scripts/dev/tidy-lane.sh` (`make tidy-lane`, `make tidy-lane-write`) copies a
checkout into a throwaway container of that image, installs the clang-tidy 22
package the hosted job installs, configures the lane from the Makefile's
`TIDY_RATCHET_COMPILERS_<lane>` / `TIDY_RATCHET_SETUP_<lane>`, builds it and
runs the ratchet. `cpu` is the hosted job's configuration, kept identical by a
contract test; the GPU lanes turn their device compiler on. A check or a
scoped write under a clang-tidy other than the one a baseline names stops;
only a full write moves a baseline to a new version. The hosted job keeps the
`cpu` lane; the other lanes run locally and from a nightly timer on the
workstation until a runner with the toolchains exists.

The maintainer's answer names the `cpu` and GPU lanes. The `arm64` cross lane
follows them into the container, with the distribution's aarch64 cross
compiler installed at the start of the run, because its only other
environment, the workstation, no longer has the baselines' clang-tidy.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Dev container with the real toolchains (chosen) | Device bodies and kernels are parsed against the headers they are built with; one image, the hosted runners' Ubuntu release; `cpu` reproduces the hosted report byte for byte | The GPU lanes are not a required check until a GPU runner exists; a run needs the 62 GB image and about five minutes per lane | n/a |
| Second pass over the device bodies on hosted runners, with stub headers | Runs as a required check today, no toolchain | Defines `HAVE_HIPCC` and its CUDA / SYCL counterparts against headers that are not the real ones, so findings differ from a real build; the kernels still cannot be parsed | Measures something that is not built |
| Leave as it is | No work | Stubs stay the linted surface; the row stays open; the `cpu` baseline keeps failing the hosted job after every header cleanup | The lanes would keep reporting numbers nothing reproduces |

How the checkout reaches the container was a second choice. A git worktree's
`.git` file points outside the container, so the container's `/workspace`
mount cannot be a worktree. Mounting the checkout read-only fails because the
lane writes into it (the Makefile's `.venv`, the rewritten baseline), and
mounting it read-write leaves files owned by the container's user in the
checkout. The script streams the tracked and the
not-yet-added files in as a tar archive and copies the results out with
`docker cp`: nothing is mounted, and uncommitted edits are measured.

## Consequences

- **Positive**: `scripts/dev/tidy-lane.sh cpu` on master `513d2a6fc` writes
  the same report as the hosted run (327 translation units, 322 findings,
  SHA-256 `ffb5ca1819a3…`, identical to the `tidy-ratchet-cpu` artifact of
  run 37011276599). The GPU lanes parse what a device runs.
- **Negative**: a lane run needs Docker, the dev image and network access
  (clang-tidy from apt.llvm.org, meson from PyPI). `.hip` kernels are parsed
  by ROCm's own clang-tidy (AMD LLVM 23.0.0git in ROCm 10.0.0), because ROCm
  10's device headers call `__builtin_amdgcn_is_invocable`, which stock LLVM
  22 rejects; a baseline records only clang-tidy 22's version, so a ROCm bump
  can move the kernel counts without a version change in the JSON.
- **Neutral / follow-ups**: the image carries neither clang-tidy 22 nor the
  aarch64 cross compiler yet, so a run installs them (the script skips the
  step once an image has them). The `arm64` baseline now names Ubuntu's cross
  gcc 15.2 where it named Arch's 16.1.
  A scoped tightening is done in the container too
  (`scripts/dev/tidy-lane.sh --write --only <file> <lane>`); a scoped write
  from a host records that host's numbers. The findings below are debt for
  the RC3 standards batches.

### Measurements

Master `0970c56f0`, clang-tidy 22.1.8 (`.hip` kernels: AMD LLVM 23.0.0git), eight
cores, about five minutes a lane.

| Lane | Findings before | after | Translation units before | after | Files down | Files up |
|---|---|---|---|---|---|---|
| `cpu` | 285 | 70 | 306 | 330 | 29 (−215) | 0 |
| `cuda` | 596 | 620 | 353 | 425 | 94 (−520) | 39 (+544) |
| `hip` | 590 | 504 | 343 | 426 | 93 (−520) | 37 (+434) |
| `sycl` | 677 | 172 | 345 | 411 | 96 (−542) | 6 (+37) |
| `arm64` | 556 | 115 | 284 | 308 | 76 (−441) | 0 |

"Before" is the committed baseline, whose translation-unit list dated from
the lane's last full measurement (2026-09-16 to 2026-09-23). What went down
had been cleaned since and could not be recorded (a scoped write cannot lower
a header's count), or was a finding of the workstation's C library.

Every file that went up, by reason:

- **Kernels, never in a compile database.** `cuda`: 21 `.cu`
  files, 473 findings in 23 `.cu` and
  `.cuh` files (`speed/speed_score.cu` 150, `integer_cambi/cambi_score.cu` 46, `integer_ms_ssim/ms_ssim_score.cu` 39, `integer_ssim/ssim_score.cu` 34, `integer_vif/filter1d.cu` 30); mostly `performance-no-int-to-ptr` (137), `misc-use-anonymous-namespace` (97), `modernize-use-designated-initializers` (61). `hip`:
  22 `.hip` files, 314 findings in
  19 (`integer_vif/vif_statistics.hip` 119, `integer_psnr_hvs/psnr_hvs_score.hip` 28, `float_adm/float_adm_score.hip` 22, `float_vif/float_vif_score.hip` 21, `ssimulacra2/ssimulacra2_device.hip` 18); mostly `bugprone-signed-bitwise` (122), `misc-const-correctness` (45), `bugprone-implicit-widening-of-multiplication-result` (26).
- **Headers a kernel includes, parsed as C++ for the first time.** `cuda`:
  14 headers, +67 (`integer_ciede/ciede_device.h` 12, `integer_cambi_cuda.h` 8, `ssimulacra2_eotf_lut.h` 6, `float_vif/float_vif_device.h` 5, `ssimulacra2_cuda.h` 5).
  `hip`: 16 headers, +108
  (`ordered_sum.h` 33, `adm_angle_flag.h` 25, `integer_cambi/cambi_hip_device.h` 13, `speed/speed_hip_device.h` 7, `float_vif_gpu_common.h` 6).
- **Translation units added after the lane's last full measurement.** The
  scoped write never extends a lane's list.
  `cuda`: `test_cuda_multi_instance.c` 2, `test_gpu_speed_lanczos4_parity.c` 1. `hip`: `test_hip_cambi_device_math.c` 11, `test_sycl_fp_arith_contract.c` 1. `sycl`: `test_sycl_shared_frame_sticky_geometry.c` 13, `test_sycl_kernel_scratch.c` 11, `test_sycl_kernel_registration.c` 3, `test_sycl_vif_min_dim.c` 1.
- **Files changed after the last full measurement, in a lane no job
  compares.** `cuda`: `libvmaf.c` 0 to 2 (the pinned picture pool of #1682). `sycl`: `integer_psnr_hvs_sycl.cpp` 0 to 5 (#1689, #1692, #1733), `test_sycl_pic_preallocation.c` 19 to 23 (#1693).

No file went up in the `cpu` and `arm64` lanes.

## References

- `Q` (maintainer popup answer, 2026-10-02, question "How should the GPU tidy
  lanes be measured?"): "In the dev container, real toolchains
  (Recommended)". The other two options: "Second pass over the device bodies
  on hosted runners", "Leave as it is".
- [ADR-1142](1142-whole-codebase-standards.md) (the ratchet),
  [ADR-1230](1230-modern-gcc-toolchain.md) (the compiler a baseline names),
  [ADR-1243](1243-tidy-scoped-baseline-tightening.md) (scoped write),
  [ADR-1283](1283-whole-tree-ratchet-arm64-lane.md) (arm64 lane),
  [ADR-1290](1290-sycl-tidy-lane-compile-database.md) (SYCL lint database).
- Hosted run 37011276599, job `Tidy Ratchet`, artifact `tidy-ratchet-cpu`.
- `docs/state.md`: `T-TIDY-RATCHET-GPU-LANES-UNREPRODUCIBLE-2026-09-22`,
  `T-TIDY-GLIBC-244-STATIC-ASSERT-FALSE-POSITIVE-2026-10-02`.
- Contributor guide: [measuring the clang-tidy lanes](../development/tidy-lanes.md).
