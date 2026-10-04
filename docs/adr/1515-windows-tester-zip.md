<!-- markdownlint-disable MD013 MD060 -->
# ADR-1515: A native Windows tester zip for x64 and Arm64, built by the hosted runners with MSVC and a static C runtime

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ci, release, windows, msvc, testing, license, supply-chain, fork-local

## Context

The maintainer asked for tester kits for the hardware the project lacks, native
Windows among them, on the condition that no licence is broken (ADR-1503). The
project has no Windows machine of its own. The hosted runners build Windows: the
`Windows MSVC+CUDA (full)` lane runs 16 CPU unit tests of an x64 MSVC build, the
`Windows ARM64 MSVC` lane runs the `fast` suite on Arm64, and the `Windows MSVC+SYCL`
lane only builds. No tester has run the fork's MSVC build on his own Windows machine,
and the MSVC build of the x86 SIMD paths (AVX2, AVX-512) has never been held to the
scalar code or to recorded scores on any machine.

A Windows tester has no compiler, no Python and often no Visual C++ runtime installed,
runs PowerShell or the Command Prompt, and meets SmartScreen and possibly Smart App
Control for an unsigned download. The report program is Python (HISS-19: one
implementation for every kit) and needs an interpreter in the package.

## Decision

We publish two zips, `vmafx-tester-windows-x64-<describe>.zip` and
`vmafx-tester-windows-arm64-<describe>.zip`, built only by
`.github/workflows/windows-tester-bundle.yml` on the hosted `windows-2025` and
`windows-11-vs2026-arm` runners, each natively for its own architecture. Source,
recipe, publishing and trust follow the macOS bundle (ADR-1493): a dispatch on master
with `ref` (a commit reachable from master) or `tag` (a published release), the
dispatching ref's recipe (ADR-1347), `publish: true` behind the `tester-publish`
environment, provenance and SBOM attestations, a cosign keyless bundle per zip, and a
prerelease `tester-windows-<date>-<sha8>` (its own prefix, so the macOS and Windows
prereleases of one commit cannot take the same tag) created by the release-bot
identity. A push to master that changes the zip's own inputs builds and verifies both
zips without publishing, so the first build on master needs no dispatch. This is an
exception to ADR-1102 with the bounds ADR-1493 sets for the macOS bundle: hosted runner
only, a commit on master, attested and signed, tester package only, never a product
release binary; it ends when a Windows build path exists in the container.

- **Build**: MSVC, `--default-library=static`, `-Db_vscrt=mt` (the C and C++ runtime
  linked statically, so no runtime DLL ships for VMAFx, ADR-1503 rule 7), no GPU
  backend, no ONNX Runtime; `vmaf.exe` and the unit tests of
  `tools/rc1-tester/image/unit-tests-windows.txt` (the Linux list plus the tests the
  MSVC lanes run on Windows and the Windows path and locale tests).
  `scripts/ci/build-windows-tester-bundle.py` builds, stages, writes the references,
  the notices and the zip; it is Python because the runner and the tests of this
  repository both have it.
- **Interpreter**: python-build-standalone 3.13.16 for `x86_64-pc-windows-msvc` and
  `aarch64-pc-windows-msvc`, pinned by URL and SHA-256 with the `full` archive of the
  same release for its licence texts, pruned as in the macOS bundle. Its
  `vcruntime140*.dll` are replaced by the unmodified copies of the runner's Visual
  Studio redistributable folder (`VC\Redist\MSVC\<version>\<arch>\Microsoft.VC14x.CRT`,
  recorded in `image/msvc-redist.json`): the only Microsoft DLLs in the zip, from the
  one folder ADR-1503 allows.
- **Report**: the same program and schema (`schema_version` 3); the schema gains the
  host platform `windows` and the package kind `windows-zip`. A Windows host reads the
  processor's name and family from its registry key and the instruction-set features
  from `IsProcessorFeaturePresent` (`hw_winfacts.py`); the dispatch flags mirror
  `core/src/x86/cpu.c`, with AVX-512 read from `PF_AVX512F` alone because Windows
  reports no CD, BW, DQ or VL bit. Architectures take their Linux names (`x86_64`,
  `aarch64`), so references and reports read the same on every kit. The report gains
  `--output`, so the launcher writes UTF-8 itself: a redirect in Windows PowerShell 5.1
  writes UTF-16, which the CI gate cannot read.
- **Launcher**: `run.cmd`, which runs from PowerShell and from the Command Prompt and
  is not subject to PowerShell's execution policy; it refuses a zip of the other
  architecture (an x64 zip on Windows on Arm would run emulated and report emulated
  features).
- **Checks before packing**: `scripts/ci/check-windows-bundle-imports.py` reads the
  import tables of every program: a VMAFx program may import Windows DLLs only (no
  `vcruntime`, `msvcp`, `ucrtbase` or `api-ms-win-crt-*`, which proves `/MT`), the
  interpreter's imports must be Windows DLLs or files of `runtime\`, and every image
  must be of the zip's architecture; then the licence gate of artifact `windows-zip`
  (ADR-1503). The SBOM (syft) and the schema check of the runner's report run on Linux.
- **Trust**: the programs carry no code signature (the project has no certificate).
  The guide downloads with `curl.exe` and unpacks with `tar.exe`, which set no
  internet mark, and checks the zip first (`Get-FileHash`, `gh attestation verify`,
  `cosign verify-blob`); after a browser download it says what SmartScreen shows and
  removes the mark only after that check. Smart App Control blocks unsigned programs;
  the guide says so and does not ask the tester to turn it off.
- **GPU twins**: not in this decision. The CUDA build exists and would ship no NVIDIA
  file, but the report's CUDA backend reads Linux device nodes and `libcuda.so.1` and
  needs a Windows port (`nvcuda.dll`). The SYCL build exists, but `-fsycl` on Windows
  forces the dynamic runtime (`/MD`; Intel's compiler guide: "You cannot specify option
  /MT"), so a SYCL zip would carry the Visual C++ runtime DLLs and the Intel runtime
  DLLs of the Windows `credist.txt`, and the report's SYCL backend would need a
  Windows Level Zero probe. Both are follow-ups with their own records.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| MinGW-w64 (UCRT64) build, as the `Windows UCRT64` lane | Static libgcc and libstdc++, nothing from Microsoft linked in | The brief asks for the MSVC-built SIMD paths; gcc on Win64 allocates 64-byte zmm spill slots the ABI cannot align (ADR-1254); the MSVC build is what Windows users build | Rejected |
| Dynamic runtime (`/MD`) with `vcruntime140.dll` and `msvcp140.dll` next to `vmaf.exe` | The default of every MSVC lane | Distributable Code to carry and pass on; Microsoft advises against app-local copies for servicing | ADR-1503 rule 7 prefers `/MT`, which ships nothing |
| Keep python-build-standalone's own `vcruntime140*.dll` | No file replaced | Those copies come from another party's Visual Studio; ADR-1503 lets a Microsoft DLL ship only from a licensed Visual Studio's redistributable folder | Replaced by the runner's copies, recorded with their SHA-256 |
| PowerShell launcher (`run.ps1`) | Native to PowerShell | Execution policy blocks unsigned scripts by default and blocks downloaded ones under RemoteSigned; a tester would have to weaken it | `run.cmd` runs everywhere without a policy change |
| Report through a redirect (`.\run.cmd > report.json`) | Same as the Linux command | Windows PowerShell 5.1 writes UTF-16 with a byte-order mark | `--output report.json`, written as UTF-8 |
| Port `build-macos-tester-bundle.sh` to one cross-platform builder | One build script | The macOS script is published and proven; rewriting it cannot be verified without a macOS run | A Windows builder now; merging the two is a follow-up once both have runs |
| One zip with x64 and Arm64 programs | One download | Twice the size; the launcher would choose at run time | One zip per architecture |
| A workflow artifact instead of a prerelease | No release | Needs a GitHub login and expires | Fallback only (`publish: false`) |
| Code-sign the programs | No SmartScreen warning | No certificate exists | Not possible; checksum, attestation and cosign instead |

## Consequences

- **Positive**: a Windows tester downloads one zip and runs one command; the MSVC
  build's AVX2 and AVX-512 paths are held to scalar and to scores recorded by the same
  build, and the MSVC-specific unit tests run on his machine; the zip carries no file
  without a recorded licence, and no Microsoft runtime DLL for the VMAFx programs.
- **Negative**: the zips are large because every test program links libvmaf
  statically (the MSVC build has no DLL, ADR-0121); the hosted runners' reports are
  the first runs of the MSVC SIMD tests on x64, so failures there are findings, not
  regressions; the Arm64 zip's report has no x86_64 cross-architecture reference (the
  section reads `missing` and is informational).
- **Neutral / follow-ups**: a Windows CUDA zip and a Windows SYCL zip; one builder for
  the macOS bundle and the Windows zip; the state row
  `T-TESTER-WINDOWS-NATIVE-EVIDENCE-2026-10-04` closes with accepted reports.

## References

- `req` (maintainer popup, 2026-10-03, paraphrased): prepare tester kits for the
  hardware the project lacks, including native Windows; no licence may be broken.
- [ADR-1503](1503-tester-artifact-licensing.md), [ADR-1493](1493-macos-tester-bundle.md),
  [ADR-1492](1492-tester-image-arm64-report.md), [ADR-1102](1102-phase4b9-container-only-publishing.md),
  [ADR-1347](1347-image-recovery-from-default-branch.md), [ADR-0121](0121-windows-gpu-build-only-legs.md),
  [ADR-1260](1260-windows-arm64-cpu-lane.md), [ADR-1254](1254-win64-cannot-realign-the-stack.md),
  [ADR-1364](1364-windows-sycl-msvc-device-link.md),
  [Research-2141](../research/2141-windows-tester-zip.md) (the sources, measurements and
  the Wine run of the report).
- [Visual Studio 2026 distributable code list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)
  (ms.date 2025-11-11, read 2026-10-04): "Visual C++ Runtime Files", `VC\redist`, not
  `debug_nonredist`; [Choose a deployment method](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method)
  (2022-06-28) and [Universal CRT deployment](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment)
  (2022-02-07) on static linking and the Universal CRT as part of Windows;
  [IsProcessorFeaturePresent](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent)
  (updated 2025-12-18); [Smart App Control FAQ](https://support.microsoft.com/en-us/windows/smart-app-control-frequently-asked-questions-285ea03d-fa88-4d56-882e-6698afdb7003);
  Intel oneAPI DPC++/C++ Compiler Developer Guide 2025.2, [`-fsycl`](https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2025-2/fsycl.html);
  python-build-standalone release 20261001 `SHA256SUMS`.
