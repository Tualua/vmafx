---
paths:
  - .github/workflows/libvmaf-build-matrix.yml
invariant: Windows CUDA installs direct packages without Jimver; Windows ARM64 native MSVC toolset and PE machine check.
---
# Windows CUDA and ARM64 runner invariants (ADR-0664 / ADR-1260)

## Windows CUDA setup path (ADR-0664)

`libvmaf-build-matrix.yml` installs CUDA 13.3.1 directly in
`Windows MSVC+CUDA` leg. Do not restore
`Jimver/cuda-toolkit` for that Windows leg without superseding ADR
and green required Windows CUDA run: v0.2.35 failed before setup on
PR #1463, blocked merge train without Meson or compiler output.

Linux CUDA legs still use `Jimver/cuda-toolkit`; ADR-0664 only
special-cases Windows network-installer path. Keep explicit
Windows package set (`nvcc`, `cudart`, `crt`, `nvvm`, and
`visual_studio_integration`) aligned with CUDA major/minor suffix
in workflow when bumping CUDA.

## Windows ARM64 lane (ADR-1260)

`windows-arm64` job in `libvmaf-build-matrix.yml`, display name
`Windows ARM64 MSVC`, advisory: no `# required-aggregator` marker, not in
`required-aggregator.yml`. Promotion = ADR amendment + aggregator entry in
same PR, maintainer decision.

Invariants:

- `runs-on: windows-11-vs2026-arm`. `windows-11-arm` migrates to same VS 2026
  image 2026-09-21..30 (runner-images #14602); switch only after that, and
  only with green run.
- `setup-msvc-dev` `arch: arm64` = `vcvarsall.bat arm64`, ARM64-hosted native
  toolset. Never `amd64_arm64` (x64 cross compiler under emulation). `Show
  compiler` step greps `cl.exe` banner for `for ARM64`; keep it, fails fast on
  wrong host toolset.
- PE machine check (`0xAA64`) on `install\bin\vmaf.exe` stays: x64 binary
  runs under emulation and would pass tests.
- Python pin = `PYTHON_CI_VERSION` like x64 legs; `actions/python-versions`
  has `win32/arm64` for it. `pip install meson ninja`: ninja ships
  `win_arm64` wheel. No nasm step; x86-only probe in `core/src/meson.build`.
- CPU only until CUDA 13.4 bump: 13.3.1 has no `windows-arm64` packages.
- Test step uses `scripts\ci\run_meson_test.py` with
  `-C core\build --suite fast --print-errorlogs`.
  Full suite =
  follow-up, own decision.
