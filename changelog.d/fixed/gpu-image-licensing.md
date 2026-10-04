- **The CUDA, ROCm and oneAPI images carry the licences of what they contain,
  ship only the vendor files `vmaf` loads, and publish the source their copyleft
  parts require.** All three are now Debian 13 images. The CUDA image holds no
  NVIDIA library (the host driver provides `libcuda`) and passes on the CUDA
  Toolkit EULA terms for the NVIDIA code inside the kernels; the ROCm image holds
  the HIP runtime files instead of AMD's whole 29 GB development image, which
  carried a profiler library whose licence forbids redistribution, and its
  kernels now cover all 25 GPU targets ROCm 10.0.0 supports; the oneAPI image
  holds the redistributable SYCL runtime files from the compiler instead of
  Intel's full runtime set, and reaches the GPU through Level Zero only. Each
  image has `/usr/local/share/vmafx/licenses/THIRD_PARTY_NOTICES.txt`, fails its
  build when a file has no recorded licence, gets an attested SPDX SBOM, and is
  published with `<tag>-cuda13-source`, `<tag>-rocm10-source` or
  `<tag>-oneapi2026-source`. The vendor toolchains (`nvcc`, `hipcc`,
  `/opt/intel/oneapi`) are no longer in the images, and the ROCm and Intel
  libraries moved to `/usr/local/lib/rocm` and `/usr/local/lib/intel`
  ([ADR-1517](docs/adr/1517-gpu-image-licensing.md),
  [production images](docs/development/docker-production.md#gpu-variants),
  [licensing](docs/licensing.md)).
