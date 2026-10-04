- **A Windows SYCL tester zip measures every SYCL twin on a tester's Intel GPU**
  (ADR-1566). `windows-tester-bundle.yml` builds a fourth zip,
  `vmafx-tester-windows-x64-sycl-<version>.zip`, with Intel's `icx-cl`. It carries the
  SYCL device tests, the parity gate, the scratch audit and the SYCL row map, and runs
  them on every Intel GPU of the PC through its own Level Zero loader. `-fsycl`
  requires the dynamic C runtime, so the Visual C++ runtime DLLs, Intel's
  `credist.txt`-listed SYCL runtime and the loader lie beside every program. The
  Windows SYCL build has never run on a GPU (`T-SYCL-WINDOWS-BUILD-NEVER-RUN-ON-A-GPU-2026-10-04`).
