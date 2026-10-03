- **Zero-copy `libvmaf_sycl` imports chroma.** On QSV / VA-API input the library now imports the 4:2:0 chroma planes
  along with luma, so `psnr` chroma (`psnr_cb`, `psnr_cr`), `psnr_hvs` chroma and
  `motion_sycl` with `motion_add_uv=true` score on zero-copy frames instead of
  failing with `-ENOTSUP`. On an Arc A380 they equal the CPU (`psnr`, `psnr_hvs`)
  and host upload (`motion_add_uv`) bit for bit at 8 and 10 bit
  ([ADR-1597](../../docs/adr/1597-sycl-zerocopy-planar-chroma-import.md)). The D3D11
  import still carries luma only; chroma readers fail there with `needs chroma
  planes, which this zero-copy import did not provide`.
