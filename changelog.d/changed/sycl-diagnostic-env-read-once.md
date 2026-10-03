- **The SYCL diagnostic switches are read once per process.** `VMAF_SYCL_PROFILE`,
  `VMAF_SYCL_TIMING`, `VMAF_SYCL_IMPORT_DEBUG` and `VMAF_SYCL_CHECKSUM` now go
  through the shared environment snapshot (`vmaf_gpu_dispatch_env_get`, ADR-0488)
  that the other SYCL switches use, instead of a `getenv()` per call. Names and
  the "value starts with `1`" meaning are unchanged, but a change made after the
  first SYCL state is created is not seen by a running process
  ([env vars](../../docs/usage/env-vars.md)).
