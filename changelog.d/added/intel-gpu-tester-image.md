- **An Intel GPU tester image measures every SYCL twin on an outside tester's
  Intel GPU with one command and no build.**
  `ghcr.io/vmafx/vmafx:<version>-tester-sycl` (linux/amd64) runs on Linux with
  `--device /dev/dri` or on Windows with WSL2 through `/dev/dxg`, finds every
  Intel GPU and reports per GPU its family, every SYCL twin against the CPU at
  `--precision max`, the parity gate's SYCL cells, the 69 SYCL device tests, the
  scratch-memory audit, and which state rows the GPU's measurements close; its
  first target is the Xe-LP half of
  `T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02` on a UHD 770. The tester
  report's schema version 3 adds a backend-neutral `gpu` section the CUDA and
  HIP kits reuse. See
  [the tester guide](docs/usage/tester-image.md#c-intel-gpu-image-linux-or-windows-with-wsl2)
  and [ADR-1505](docs/adr/1505-intel-gpu-tester-image.md).
