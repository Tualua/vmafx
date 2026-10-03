<!-- markdownlint-disable MD013 -->
# Research-2136: what an Intel GPU tester image needs at run time, on Linux and under WSL2

- **Date**: 2026-10-03
- **Status**: input to [ADR-1505](../adr/1505-intel-gpu-tester-image.md)
- **Measured on**: Arc A380 (`dg2-g11`, IP 12.56.5, xe driver, Linux 7.2), in the
  image built from `docker/Dockerfile.tester --target final-sycl`.

## Question

Which files does a SYCL build of libvmaf load to run its kernels on an Intel GPU,
which Intel GPUs does the pinned compute runtime drive, and how does a container
reach an Intel GPU under WSL2?

## Sources

- `intel/compute-runtime` release 26.35.39758.10 (the `INTEL_NEO_VERSION` of
  `build-config.env`): supported platforms "Tiger Lake, Rocket Lake, Alder Lake,
  Raptor Lake, Meteor Lake, Arrow Lake, Lunar Lake, Panther Lake, Wildcat Lake,
  DG1, Alchemist (DG2), and Battlemage" at production quality; IGC 2.41.5, gmmlib
  22.10.0; "WSL support was tested for all platforms with Windows host driver
  101.8991".
- `intel/compute-runtime` `documentation/WSL.md`: Windows 11 host, WSL kernel
  6.1 or later, the Intel Windows driver (the Arc driver for Xe and later, the
  "11th-14th Gen" driver for older integrated GPUs), and the latest compute
  runtime release inside WSL.
- `intel/compute-runtime` `shared/source/dll/linux/options_linux.inl`: the runtime
  opens `/usr/lib/wsl/lib/libdxcore.so` by that absolute path.
- [Research-2128](2128-oneapi-release-image-runtime.md): this compute runtime ran
  SYCL kernels on an Arc B580 and a UHD 770 under Docker Desktop's WSL2 backend
  on Windows 11 with `/dev/dxg` passed through and `/usr/lib/wsl` mounted.
- `ocloc ids` of compute-runtime 26.35 for the GPU IP version of every target of
  the default AOT list (`tgllp` 12.0, `adl-s`/`rpl-s` 12.2, `adl-p`/`rpl-p` 12.3,
  `dg1` 12.10, `dg2-g11` 12.56.5, `pvc` 12.60.7, `mtl-u` 12.70.4, `arl-h`
  12.74.4, `bmg-g21` 20.1.0, `lnl-m` 20.4.4, `ptl-h` 30.0.4).
- The Intel End User License Agreement for Developer Tools (Version August 2024)
  and `credist.txt` of the oneAPI DPC++/C++ Compiler 2026.1.1.

## Findings

1. **The SYCL runtime a GPU run loads is small.** `ldd` of `libvmaf.so` and of the
   Unified Runtime adapters names `libsycl.so.9`, `libur_loader.so.0`,
   `libur_adapter_level_zero.so.0` and `_v2`, `libsvml`, `libimf`, `libintlc`,
   `libirng`, `libirc`, `libumf.so.1` and `libhwloc.so.15`: 77 MB, every
   compiler file among them in `credist.txt`. Intel's runtime meta-package
   installs 1.1 GB. With `libsycl-jit`, `libomptarget`, `icx-lto.so`,
   `libintelocl` and `libcommon_clang` moved away, `test_sycl_exact_twins`,
   `test_sycl_kernel_scratch`, `test_sycl_ciede_parity` and
   `test_sycl_psnr_hvs_parity` pass on the A380.
2. **SPIR-V needs neither OpenCL nor IGC's OpenCL front end.** A SYCL program
   built with `-fsycl-targets=spir64` only (so the kernel is compiled from SPIR-V
   at run time) returns correct values on the A380 after `intel-opencl-icd`,
   `intel-igc-opencl-2` and `ocl-icd-libopencl1` are purged, with
   `NEO_CACHE_PERSISTENT=0`; so do the device tests. The Level Zero driver needs
   IGC core and gmmlib.
3. **A family follows from the GPU IP version.** Level Zero's
   `ze_device_ip_version_ext_t` gives `(major << 22) | (minor << 14) | revision`;
   the A380 reports 12.56.5, which `ocloc ids dg2-g11` prints. Xe-LP is 12.0 to
   12.4 and 12.10, Xe-HPG 12.55 to 12.57, Xe-HPC 12.60, Xe-LPG 12.70 to 12.74,
   Xe2 20.x, Xe3 30.x. A UHD 770 (Alder Lake-S or Raptor Lake-S) is 12.2.
4. **The hardened run works on Linux.** `--read-only --cap-drop ALL
   --security-opt no-new-privileges` with `--device /dev/dri` and the render
   group added runs every device test as uid 10001; one test writes into its
   working directory, so each test runs in a fresh directory under `/tmp`.
5. **WSL2 is reachable but not measured with this image.** Under WSL2 the GPU is
   `/dev/dxg` and the host driver's user-mode half sits in `/usr/lib/wsl/lib`;
   the compute runtime of this image is the one Research-2128 ran there. What
   that run did not cover: the read-only, capability-free run as a non-root user,
   the permissions of `/dev/dxg` inside the container, and the "11th-14th Gen"
   Windows driver a UHD 770 needs.

## Open questions

- The first WSL2 report (its `gpu.access.path` is `wsl`) answers finding 5.
- Whether the Xe-LP slot kernel of `ssimulacra2_sycl` uses scratch memory on a
  UHD 770, as the build log predicts (the state row), is the measurement the kit
  exists for.
