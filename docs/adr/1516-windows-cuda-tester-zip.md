<!-- markdownlint-disable MD013 MD060 -->
# ADR-1516: A Windows CUDA tester zip that ships no NVIDIA file and measures every CUDA twin on a tester's Windows PC

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ci, release, windows, msvc, cuda, testing, parity, license, fork-local

## Context

The Windows tester zip (ADR-1515) is CPU only. The fork's MSVC CUDA build exists: the
`Windows MSVC+CUDA` lanes build it with nvcc and MSVC on every relevant change, but the
runner has no GPU, so no CUDA kernel of a Windows build has ever run. The NVIDIA GPU
tester image (ADR-1509) measures every CUDA twin on Linux and under WSL2; a native
Windows run would be the first of the MSVC build's host code (the CUDA runtime glue,
the Win32 thread shim under the device tests) on a GPU. The maintainer's condition for
every kit stands: no licence may be broken (ADR-1503).

Two facts carry over from the Linux kit. `libvmaf` links no NVIDIA library: it loads
the driver at run time through the nv-codec-headers loader, which on Windows opens the
display driver's `nvcuda.dll`. The kernels' device code holds CUDA header code and
`libdevice`, which Attachment A of the CUDA Toolkit EULA lists as distributable in
object code.

## Decision

We add a third zip to `.github/workflows/windows-tester-bundle.yml`,
`vmafx-tester-windows-x64-cuda-<describe>.zip`: the CPU zip's build with
`-Denable_cuda=true -Denable_nvcc=true`, still `/MT`, built on `windows-2025` with the
toolkit `scripts/ci/install-cuda-toolkit.ps1` installs at `build-config.env`'s
`CUDA_VERSION` and nv-codec-headers at the commit `docker/Dockerfile.tester` pins. It
carries the CPU zip's checks plus the `gpu` section of the CUDA backend (`hw_cuda.py`)
with the Linux kit's device tests (`cuda-tests.txt`), parity gate, twin bounds, build
targets and row map (`cuda-rows.json`); x64 only (no Windows on Arm device has an
NVIDIA GPU).

- **No NVIDIA file ships.** The import check of ADR-1515 refuses any program that
  imports a DLL outside Windows, so a static link to `cudart` or another NVIDIA library
  fails the build; `nvcuda.dll` stays the tester's driver's.
- **Licences**: artifact `windows-cuda-zip`, the `windows-zip` record plus
  `nvidia-cuda-device-code` (the CUDA EULA, copied from the toolkit's `LICENSE` and
  checked for the text ADR-1509 read: "Last updated: January 26, 2026" and
  `libdevice.10.bc`) and `nv-codec-headers` (the two headers' MIT notices, copied at
  build time), as in the Linux kit. The notes say the zip ships no NVIDIA library.
- **Report**: `hw_cuda.py` reaches the GPU through `nvcuda.dll` in System32 (path
  `windows`, or `none` with the driver to install); `hw_cudaprobe.py` loads that DLL
  with the same driver calls. The hosted runner has no GPU, so the verify job requires
  `gpu.status` `no_device` naming `nvcuda.dll`.
- **Not in this decision**: a Windows SYCL zip. `-fsycl` forces the dynamic runtime on
  Windows ("You cannot specify option /MT", Intel compiler guide 2025.2), so that zip
  would carry the Visual C++ runtime DLLs for VMAFx programs and Intel's Windows
  `credist.txt` files, and the report's SYCL backend would need a Windows Level Zero
  probe; it gets its own decision.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| CUDA in the CPU zip | One x64 zip | Every x64 tester downloads the fatbins (six cubins and two PTX per kernel in every test program) and the device tests | A separate zip for the testers with an NVIDIA GPU |
| Ship `cudart64_13.dll` (Attachment A) | Would allow a cudart link later | Nothing links it; an unused vendor file is what ADR-1503 rule 1 forbids | Not needed |
| Leave Windows CUDA to the WSL2 path of the Linux image | No new package | WSL2 runs the Linux build, not the MSVC one; the Windows CUDA host code would still never run | The MSVC build is what the zip exists to measure |
| Wait for a project Windows GPU host | Proof before publishing | The project has none and buys none | The tester's report is the proof; the guide says so |

## Consequences

- **Positive**: one command runs every CUDA twin of the MSVC build against the CPU on a
  tester's Windows PC; the first evidence that the Windows CUDA build works on a GPU.
- **Negative**: nothing proves the zip on a GPU before a tester runs it; the hosted run
  shows the build, the licence gate and `no_device` only. The zip is larger than the
  CPU zip by the fatbins in every program.
- **Neutral / follow-ups**: the state row `T-CUDA-WINDOWS-BUILD-NEVER-RUN-ON-A-GPU-2026-10-04`
  closes with an accepted report; a CUDA version bump changes the EULA check of the
  build and of `docker/Dockerfile.tester` together.

## References

- `req` (maintainer popup, 2026-10-03, paraphrased): prepare tester kits for the
  hardware the project lacks, including native Windows; no licence may be broken.
- [ADR-1515](1515-windows-tester-zip.md), [ADR-1509](1509-nvidia-gpu-tester-image.md),
  [ADR-1503](1503-tester-artifact-licensing.md), [ADR-0121](0121-windows-gpu-build-only-legs.md),
  [ADR-1223](1223-cuda-ampere-architecture-floor.md),
  [Research-2141](../research/2141-windows-tester-zip.md).
- CUDA 13.4.2 Windows redistributable archives (`redistrib_13.4.2.json`): every archive's
  `LICENSE` is the CUDA Toolkit EULA, "Last updated: January 26, 2026" (read 2026-10-04);
  [CUDA Toolkit EULA](https://docs.nvidia.com/cuda/eula/index.html) 1.1.1, 1.1.2, 1.2,
  Attachment A.
