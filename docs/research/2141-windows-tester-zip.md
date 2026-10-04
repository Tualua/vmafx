<!-- markdownlint-disable MD013 MD060 -->
# Research-2141: Windows tester zip

- **Status**: Active
- **Workstream**: [ADR-1515](../adr/1515-windows-tester-zip.md)
- **Last updated**: 2026-10-04

What the Windows tester zip is built from, what ships in it and under which terms,
what was measured without a Windows machine, and what only the hosted runners and a
tester's machine can prove.

## What the hosted Windows lanes already show

Read from run 37163432688 of `libvmaf-build-matrix.yml` (2026-10-04), jobs
`Windows MSVC+CUDA`, `Windows ARM64 MSVC` and `Windows MSVC+SYCL`:

| Fact | Value |
| :--- | :--- |
| Visual Studio on `windows-2025` and `windows-11-vs2026-arm` | Visual Studio 2026 Enterprise (`Microsoft Visual Studio\18\Enterprise`), MSVC 19.51.36260, `link` 14.51.36260 |
| Windows SDK | 10.0.26100.0 |
| `nasm` (x64 only) | `C:\Strawberry\c\bin\nasm.exe`, found by Meson |
| `xxd` (embeds the models) | `C:\Program Files\Git\usr\bin\xxd.exe`, found by Meson on both architectures |
| Runtime library of the MSVC lanes | `/MD` (Meson's default for a release build) |
| `Windows ARM64 MSVC`, `fast` suite | 267 passed, 0 failed, 1 skipped |
| `Windows MSVC+CUDA (full)` (build.yml) | 16 CPU unit tests; no x86 SIMD unit test runs on an MSVC build anywhere |

`cl.exe` ignores the `-mavx512*` arguments of the AVX-512 libraries (warning D9002):
MSVC compiles AVX-512 intrinsics without an architecture switch, so the AVX-512 paths
are built. Whether they return the scalar's values has never been measured on MSVC;
the zip's report on the hosted runner (whose CPU may or may not have AVX-512) and on a
tester's machine are the first measurements.

## Licences of what ships

Read on 2026-10-04 unless stated.

| Component | Files | Terms | Source of the terms |
| :--- | :--- | :--- | :--- |
| VMAFx | `build\tools\vmaf.exe`, `tests\*.exe`, the report program | EUPL-1.2 and the inherited licences, computed per compiled file (`licensing.py scan-build`) | the files' SPDX headers and `REUSE.toml` |
| Microsoft C and C++ runtime, statically linked | inside every VMAFx program (`/MT`) | Visual Studio licence; the STL is Apache-2.0 WITH LLVM-exception | [Choose a deployment method](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method) (2022-06-28): static linking is a way to redistribute the Visual C++ libraries, discouraged for servicing; [Universal CRT deployment](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment) (2022-02-07): "Statically link the Universal CRT" is a listed deployment, and the UCRT is part of Windows 10 and later; [microsoft/STL](https://github.com/microsoft/STL) licence |
| `vcruntime140.dll`, `vcruntime140_1.dll` (interpreter) | `runtime\` | Microsoft Distributable Code | [Visual Studio 2026 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution) (ms.date 2025-11-11): "you may copy and distribute with your program any of the files within the following folder and its subfolders ... [VisualStudioFolder]\VC\redist", never `debug_nonredist`, "unmodified form" |
| CPython 3.13.16 | `runtime\` | PSF-2.0, `Doc/license.rst` (pinned SHA-256), HACL\* MIT | [python-build-standalone licensing](https://gregoryszorc.com/docs/python-build-standalone/main/running.html), the `full` archive's `python/licenses/` |
| Libraries python-build-standalone ships on Windows | `runtime\DLLs\` (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`, `libffi-8.dll`, `sqlite3.dll`) and inside `python313.dll` and the `.pyd` modules | OpenSSL Apache-2.0, libffi MIT, SQLite public domain, expat MIT, mpdecimal BSD-2-Clause, bzip2, liblzma 0BSD, zlib | `PYTHON.json` and `python/licenses/` of `cpython-3.13.16+20261001-*-pc-windows-msvc-pgo-full.tar.zst` |
| Netflix test videos | `python\test\resource\` | BSD-2-Clause-Patent | `Netflix/vmaf_resource` `LICENSE` at the pinned commit |

Nothing copyleft ships: the zip has no source companion. The text of the Visual
Studio 2026 licence terms (its "Distributable Code" section) could not be read as
text: `visualstudio.microsoft.com/license-terms/vs2026-ga-pro-enterprise/` renders
the terms with a script, and the served HTML holds only the title and a date
(2025-10-31). The Visual Studio 2015 SDK terms, served as text, show the form of that
section: Distributable Code may be distributed in object code "if you add significant
primary functionality", with terms that "protect the Distributable Code at least as
much as this agreement", without Microsoft's trademarks. The notices pass those terms
on for both Microsoft components.

## The interpreter

python-build-standalone 20261001, the release the macOS bundle pins:

| Archive | SHA-256 (from the release's `SHA256SUMS`) |
| :--- | :--- |
| `cpython-3.13.16+20261001-x86_64-pc-windows-msvc-install_only_stripped.tar.gz` | `c402eb9a35aba90de319bbc87d23e6d887eca1551a05cfa80775f92dcf3ebc08` |
| `cpython-3.13.16+20261001-aarch64-pc-windows-msvc-install_only_stripped.tar.gz` | `0c5f9dedc4302b74600046a905f36a654a0710c1940bed202a4644efcba5e86f` |
| `cpython-3.13.16+20261001-x86_64-pc-windows-msvc-pgo-full.tar.zst` | `fbccab8d1487f327874956eb2d0e979612c133f3619b6e86e4ec250cb0191b55` |
| `cpython-3.13.16+20261001-aarch64-pc-windows-msvc-pgo-full.tar.zst` | `d88129f3600d12887a9fee50847d71eac7127b9be20785fa759e4459b9c1bee8` |

The x64 `install_only_stripped` archive unpacks to 68 MB and ships its own
`vcruntime140.dll` and `vcruntime140_1.dll` (file version 14.44.35211.0, Visual Studio
2022). The import tables of every program in it, read with
`scripts/ci/check-windows-bundle-imports.py`'s parser: `python.exe`, `python313.dll`
and every `.pyd` import `vcruntime140.dll` and the Universal CRT API sets; only
`_wmi.pyd` imports `vcruntime140_1.dll`; `zlib1.dll` (imports the legacy
`msvcrt.dll`) is used only by `tcl86t.dll`; pip's launchers include x86 and Arm64
programs. Pruned as the build does (headers, import libraries, pip, Tcl/Tk with
`zlib1.dll`, test modules, IDLE, `pythonw.exe`) the interpreter is 31 MB in 582 files,
and every remaining import is a Windows DLL or a file of `runtime\`.

## Host facts without `/proc/cpuinfo`

`IsProcessorFeaturePresent` (Microsoft Learn, updated 2025-12-18) reports SSE2 (10),
SSE3 (13), SSSE3 (36), SSE4.1 (37), SSE4.2 (38), AVX (39), AVX2 (40), AVX512F (41) and,
from Windows 11 24H2, BMI2 (60) and the Arm SVE family (46 onwards); SSSE3 to AVX512F
need Windows 10 2004. It has no bit for AVX-512 CD, BW, DQ or VL, which
`core/src/x86/cpu.c` also requires: the report reads AVX-512 from AVX512F and says so
in `dispatch_flags_source` (the processors with AVX512F but without BW, DQ and VL are
Xeon Phi parts). The processor's name, vendor and family come from
`HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0` (`ProcessorNameString`,
`VendorIdentifier`, `Identifier`), mapped to the keys of the existing allow-list.

## What was run here, without Windows

- The report path under Wine 11.19 on `ryzen-4090-arc`: a MinGW-w64 (GCC 16.2) cross
  build of `vmaf.exe` and the 51 tests of the Windows list it has, staged as the zip
  is (relative test paths), with the pruned x64 interpreter above. Under Wine,
  `hw_winfacts` read the processor from the registry and the features from
  `IsProcessorFeaturePresent` (`AMD Ryzen 9 9950X3D`, flags `sse2 ssse3 sse4.1 avx2
  avx512`); `generate-reference` wrote both references; `wine cmd /d /c run.cmd` ran
  the report end to end in 55 s and wrote `report.json` through `--output`: dispatch
  equivalence identical, reference equivalence identical, 51 of 51 unit tests passed,
  verdict `pass`. `check-hardware-reports.py --report` accepts the schema and refuses
  the report only because a local build is not the hosted workflow's. This proves the
  Python side (registry and kernel32 calls, the bounded runner on Windows, paths,
  the launcher); it says nothing about the MSVC build.
- The import check on that MinGW tree finds only the Universal CRT imports of the
  MinGW programs (a `/MD`-like build), which is the finding it exists for; the pruned
  interpreter passes.
- The `windows-zip` licence record against a tree of that shape with the real
  interpreter: notices written, check passes; with a Microsoft DLL planted in `tests\`
  and `runtime\LICENSE.txt` removed, the check fails on both.
- Measured sizes of the MinGW tree: tests 548 MB (about 11 MB per program: static
  libvmaf, the built-in models, symbols), `vmaf.exe` 16 MB, interpreter 31 MB,
  fixtures 56 MB. MSVC programs carry no symbols in the executable; the zip's real
  size comes from the hosted build.

## Found on the way

The macOS bundle published as `tester-20261003-c12763f3` lists its unit tests by their
absolute path on the hosted runner, so no unit test starts on a tester's Mac
(`T-TESTER-BUNDLE-UNIT-PATHS-ABSOLUTE-2026-10-04`, fixed in a separate pull request;
the Windows zip depends on that fix).

## Not proven before the hosted run

- That the MSVC build links cleanly with `-Db_vscrt=mt` (no lane has used it).
- Which unit tests of the x86 SIMD list fail on MSVC, and whether the hosted x64
  runner has AVX-512.
- `compression.zstd` in the runner's Python 3.14.8 on both architectures (the build
  falls back to a `zstd` program on PATH and fails with a message without either).
- The Arm64 redistributable folder holding both `vcruntime140.dll` and
  `vcruntime140_1.dll` (the build fails with the missing name if not).
- Anything on a tester's machine: SmartScreen and Smart App Control behaviour, and the
  scores of his processor.
