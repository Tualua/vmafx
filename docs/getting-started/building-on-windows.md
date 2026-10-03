# Building on Windows

Windows has three build routes. This page covers the first and the details of
the other two; the [Windows MSVC install page](install/windows.md) has the
setup script for MSVC.

| Route | Compiler | GPU backends | CI job ([`libvmaf-build-matrix.yml`](https://github.com/VMAFx/vmafx/blob/master/.github/workflows/libvmaf-build-matrix.yml)) |
| --- | --- | --- | --- |
| [MSYS2 / MinGW-w64](#msys2-and-mingw-w64) | GCC (UCRT64) | none | `Windows UCRT64`: builds and runs the tests |
| [Native MSVC, x64](#native-msvc-and-cuda) | `cl.exe` | CUDA; SYCL with `icx-cl` | `Windows MSVC+CUDA`, `Windows MSVC+SYCL`: build and install, no GPU tests |
| [Native MSVC, ARM64](#native-msvc-on-windows-arm64) | `cl.exe` for ARM64 | none | `Windows ARM64 MSVC`: builds and runs the fast tests |

The Python harness is the same on every platform: create a virtual environment
and run `pip install python/` from the repository root.

## MSYS2 and MinGW-w64

1. Install [MSYS2](https://www.msys2.org/).
2. Open an **MSYS2 UCRT64** shell and install the toolchain:

    ```bash
    pacman -S --noconfirm --needed \
      mingw-w64-ucrt-x86_64-gcc \
      mingw-w64-ucrt-x86_64-meson \
      mingw-w64-ucrt-x86_64-ninja \
      mingw-w64-ucrt-x86_64-nasm \
      mingw-w64-ucrt-x86_64-pkg-config
    ```

3. From the repository root in the same shell, configure a static build. This
   example installs into `C:/vmaf-install`; change `--prefix` for another
   location:

    ```bash
    meson setup build core \
      --buildtype release \
      --default-library static \
      --prefix C:/vmaf-install \
      -Denable_cuda=false -Denable_sycl=false
    ```

4. Build, install and test:

    ```bash
    meson install -C build
    python3 scripts/ci/run_meson_test.py -- -C build
    ```

The 32-bit MINGW32 environment is not supported: its scores do not match. The
CI job also runs `scripts/ci/check-win64-stack-alignment.py`, because GCC's
MinGW target can emit AVX-512 spills that the Windows x64 stack alignment does
not allow ([ADR-1254](../adr/1254-win64-cannot-realign-the-stack.md)).

## Native MSVC and CUDA

The native MSVC CUDA build compiles, links and installs in CI; the hosted
Windows runner has no GPU, so it runs no GPU scoring tests.

CUDA needs Visual Studio Build Tools and the Windows SDK, even when the host
library is built with MinGW. Meson looks for `cl.exe` in two places:

1. through `vswhere`;
2. if that finds nothing, on `PATH`, as in an x64 Native Tools Command Prompt.

The compiler it finds is passed to NVCC as `-ccbin` and used for MSVC header
discovery. If neither place has a compiler, configuration stops and says that
Visual Studio Build Tools are required.

A regression test checks this lookup on any POSIX host, without a Windows SDK
or a GPU, using stubbed compiler responses:

```sh
python3 core/test/test_windows_cuda_compiler_discovery.py -v
```

It also runs in the Meson `fast` suite. Native compilation and GPU runtime
validation are separate checks.

## Native MSVC on Windows ARM64

libvmaf builds natively on Windows on ARM64 with the ARM64-hosted MSVC
toolset. The NEON kernels under `core/src/feature/arm64/` are compiled and
used, as on Linux and macOS AArch64. CI builds this recipe on the
`windows-11-vs2026-arm` runner
([ADR-1260](../adr/1260-windows-arm64-cpu-lane.md)).

You need:

- Visual Studio 2022 17.4 or later (CI uses 2026) with the **MSVC ARM64/ARM64EC
  build tools** component;
- Python 3.11 or later, with `pip install meson ninja`;
- no NASM: ARM64 has no assembly sources.

Build from an **ARM64 Native Tools Command Prompt**, or after running
`vcvarsall.bat arm64` from Visual Studio's `VC\Auxiliary\Build` directory:

```bat
cd <vmaf-repo-root>
set CFLAGS=/experimental:c11atomics
set CXXFLAGS=/experimental:c11atomics
meson setup build core --buildtype release ^
  --prefix %CD%\install ^
  --default-library=static ^
  -Denable_cuda=false -Denable_sycl=false ^
  -Denable_float=true
ninja -C build install
python scripts\ci\run_meson_test.py -- -C build --suite fast
```

`/experimental:c11atomics` is required on every MSVC build: libvmaf uses C11
atomics, and MSVC's `<stdatomic.h>` refuses them without it.

To confirm the toolset:

- `cl.exe` prints `for ARM64` in its banner when the ARM64-hosted toolset is
  active.
- The x64 cross toolset (`vcvarsall.bat amd64_arm64`) also produces ARM64
  binaries, but runs the compiler under emulation.
- `install\bin\vmaf.exe` is an ARM64 image (PE machine `0xAA64`).

How this build differs from the other AArch64 builds:

- **Strict floating point.** GCC and clang get `-ffp-contract=off` for the
  float NEON carve-outs; MSVC gets `/fp:precise`, which generates no fused
  multiply-adds by default (`arm64_strict_fp_args` in `core/src/meson.build`).
- **No SVE2.** MSVC has no `<arm_sve.h>`, and SVE2 is probed at run time on
  Linux only, so the binary dispatches NEON.
- **No CUDA.** NVIDIA published no Windows ARM64 packages for CUDA 13.3.1, the
  release this was checked against.
