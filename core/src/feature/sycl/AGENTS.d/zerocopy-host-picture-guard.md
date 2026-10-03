---
paths:
  - core/src/feature/sycl/*_sycl.cpp
  - core/src/libvmaf.c
  - core/test/test_sycl_zerocopy_guards.c
invariant: A SYCL submit that reads host pictures calls vmaf_sycl_require_host_pictures first; one guard row per SYCL extractor.
---
<!-- markdownlint-disable MD013 MD060 -->
# Zero-copy host-picture guard (ADR-1595)

`vmaf_read_pictures_sycl` passes NULL `ref_pic` / `dist_pic` to every SYCL
extractor: the data is already in the shared device slots. A `submit` that
reads host pictures (`picture_copy`, `copy_y_plane`, `stage_raw_luma`,
`fadm_upload_planes`, `upload_vif_pictures`, ...) calls
`vmaf_sycl_require_host_pictures(<registered name>, ref, dis)` first and
returns `-ENOTSUP` when it fails; the helper logs the one error line.

**On rebase**: a SYCL submit that reads host pictures calls
`vmaf_sycl_require_host_pictures` first; never dereference `ref_pic` /
`dist_pic` on the nocopy path. `test_sycl_zerocopy_guards` has one row per
SYCL extractor and counts the registrations, so a new `_sycl` extractor
without a row fails that test.

`psnr_sycl` and `psnr_hvs_sycl` read chroma, so their chroma branch calls
`vmaf_sycl_require_chroma` instead (ADR-1597): host pictures upload as
before; NULL pictures pass only when the zero-copy import marked the chroma
for this frame, else `-ENOTSUP`. **On rebase**: keep it ahead of any read of
`vmaf_sycl_get_shared_plane(.., 1|2)`; `test_sycl_zerocopy_parity` pins it.
