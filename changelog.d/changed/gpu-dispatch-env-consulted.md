- **`VMAF_CUDA_DISPATCH` is read, `VMAF_HIP_DISPATCH` is gone
  ([ADR-1571](docs/adr/1571-gpu-dispatch-env-consulted.md)).** Neither
  variable did anything: the functions that read them were never called.
  libvmaf now reads `VMAF_CUDA_DISPATCH` when a CUDA extractor initialises,
  keyed by the extractor's registered name (`VMAF_CUDA_DISPATCH=vif_cuda:graph`
  logs that graph capture is not implemented and runs direct).
  `VMAF_HIP_DISPATCH`, a per-feature HIP switch no other backend has, and the
  unused `vmaf_hip_dispatch_supports()` are removed with their docs; HIP
  routing is unchanged, and setting the variable still has no effect.
