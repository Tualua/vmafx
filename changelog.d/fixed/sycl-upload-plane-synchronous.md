- **`vmaf_sycl_upload_plane()` now returns after its copy has completed.**
  It used to enqueue the host-to-device copy and return, while nothing made
  the extractors wait for it and the documentation did not say how long
  `src` had to stay valid. On Windows `vmaf_sycl_import_d3d11_surface()`
  unmapped its staging texture straight after the call, so the copy could
  read unmapped memory. Callers may now free, unmap or refill `src` as soon
  as the call returns; see [docs/api/gpu.md](docs/api/gpu.md).
