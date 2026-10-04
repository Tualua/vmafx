- **A Windows CUDA tester zip measures every CUDA twin on a tester's Windows PC**
  (ADR-1516, `T-CUDA-WINDOWS-BUILD-NEVER-RUN-ON-A-GPU-2026-10-04`).
  `vmafx-tester-windows-x64-cuda-<version>.zip` is the Windows tester zip with
  the MSVC build's CUDA backend: `run.cmd` runs the CPU checks and, on every
  NVIDIA GPU of the RTX 30 series or newer, every CUDA twin against the CPU,
  the parity gate and the CUDA device tests, through the display driver's
  `nvcuda.dll`. The zip ships no NVIDIA file; it carries the CUDA Toolkit EULA
  for the NVIDIA code inside the kernels and the nv-codec-headers notices. It
  is the first run of the Windows CUDA build on a GPU: the hosted Windows lanes
  only compile it. See
  [the tester guide](docs/usage/tester-image.md#with-an-nvidia-gpu-the-cuda-zip).
