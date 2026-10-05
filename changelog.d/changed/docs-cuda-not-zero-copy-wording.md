- The CUDA / NVDEC path of the `libvmaf_cuda` FFmpeg filter is no longer called
  "zero-copy" in the documentation: no frame goes through host memory, but each
  decoded frame is copied device to device into libvmaf's picture pool
  (ADR-1685). The HIP upload page no longer says the CUDA backend imports
  external memory (no source file does), and Research-0086 carries a dated note
  that its licence lines predate ADR-1250.
