---
paths:
  - core/src/feature/sycl/*_sycl.cpp
  - core/src/libvmaf.c
  - core/test/test_sycl_zero_copy_admission.c
  - core/test/test_sycl_zerocopy_parity.c
invariant: SYCL submits read the shared planes, never host pictures; chroma readers gate on require_chroma.
---
<!-- markdownlint-disable MD013 MD060 -->
# Zero-copy input: no extractor reads host pictures (ADR-1766, ADR-1768)

`vmaf_read_pictures_sycl` passes NULL `ref_pic` / `dist_pic` to every SYCL
extractor: the data is already in the shared device slots. Since ADR-1766 no
SYCL `submit` reads a host picture for luma: each takes the shared planes
(`vmaf_sycl_get_shared_plane`) after `vmaf_sycl_queue_after_upload`, on the
host-picture path and the zero-copy path alike, so both feed the kernels the
same bytes; never dereference `ref_pic` / `dist_pic` in a luma path. Every
SYCL twin answers ADR-1688's admission hook true (ADR-1768);
`test_sycl_zero_copy_admission` pins each extractor's answer, so a new `_sycl`
extractor needs a row there.

`float_motion_sycl` takes `motion_add_uv` (ADR-1767): the option sits at the CPU
table's position with alias `mau`, so the feature names match; Cb / Cr are read
from the shared chroma planes behind `vmaf_sycl_require_chroma`, each plane
runs the blur and the ADR-1411 row-SAD kernels (`FmPlane`), and
`frame_sad_score()` adds the plane scores in `double`, Y then U then V. **On
rebase**: keep that order and the one `h_row_sad` readback; no device reduction
of the planes, no fp64, no scratch (`test_sycl_kernel_scratch`).
`test_sycl_zerocopy_parity` (`motion_add_uv`, `==` on host upload and zero-copy)
guards it.

`float_ms_ssim_sycl` converts the shared planes with `plane_to_float()`
(`integer_ms_ssim_sycl.cpp`), which is `picture_copy()`'s arithmetic
(`float(sample) / scaler + offset`, scalers 4 / 16 / 256 for 10 / 12 / 16 bit,
none at 8 bit). It lives in this strict-FP feature TU, never in
`sycl_sources`, and stays free of fp64 and private arrays. With
`enable_chroma` it reads Cb / Cr behind `vmaf_sycl_require_chroma`.

`psnr_sycl` and `psnr_hvs_sycl` read chroma, so their chroma branch calls
`vmaf_sycl_require_chroma` instead (ADR-1765): host pictures upload as
before; NULL pictures pass only when the zero-copy import marked the chroma
for this frame, else `-ENOTSUP`. **On rebase**: keep it ahead of any read of
`vmaf_sycl_get_shared_plane(.., 1|2)`; `test_sycl_zerocopy_parity` pins it.
