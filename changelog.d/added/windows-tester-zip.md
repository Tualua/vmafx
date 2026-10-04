- **Windows tester zips for x64 and Arm64** (ADR-1515,
  `T-TESTER-WINDOWS-NATIVE-EVIDENCE-2026-10-04`). A tester unpacks
  `vmafx-tester-windows-<x64|arm64>-<version>.zip` and runs `run.cmd` from
  PowerShell or the Command Prompt; it writes the same JSON report as the other
  tester packages. The zip holds the MSVC build of `vmaf.exe` and its unit tests
  with the C runtime linked in (`/MT`), the Netflix test videos and a bundled
  Python interpreter, so nothing needs to be installed. The report compares the
  MSVC build's AVX2 and AVX-512 (or NEON) code with its scalar code and with
  scores recorded by the same build, and runs the SIMD, dispatch and
  Windows-only unit tests. The zips are built by the hosted Windows runners
  (`.github/workflows/windows-tester-bundle.yml`), carry their licence notices,
  and are published with a build attestation, an attested SPDX SBOM and a cosign
  signature as `tester-windows-*` prereleases. The report gains `--output` to
  write the JSON as UTF-8, the host platform `windows` and the package kind
  `windows-zip`. See [section F of the tester guide](docs/usage/tester-image.md#f-native-windows-zip-x64-or-arm64).
