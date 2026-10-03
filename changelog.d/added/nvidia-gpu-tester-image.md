- **An NVIDIA GPU tester image measures every CUDA twin on an outside tester's
  NVIDIA GPU with one command and no build.**
  `ghcr.io/vmafx/vmafx:<version>-tester-cuda` (linux/amd64) runs on Linux with
  `--gpus all` (NVIDIA Container Toolkit; CDI `--device nvidia.com/gpu=all`
  works too) and, not yet proven, on Windows with WSL2 under Docker Desktop. It
  finds every NVIDIA GPU of compute capability 8.0 or newer and reports per GPU
  its family, compute capability and the kernel code it ran, every CUDA twin
  against the CPU at `--precision max`, the parity gate's CUDA cells, the 66
  CUDA device tests, and the verdict of
  `T-CUDA-TWINS-OTHER-ARCHITECTURES-2026-10-03` for its family. The image ships
  no NVIDIA file: it uses the host's driver. See
  [the tester guide](docs/usage/tester-image.md#d-nvidia-gpu-image-linux-or-windows-with-wsl2)
  and [ADR-1509](docs/adr/1509-nvidia-gpu-tester-image.md).
