- **The SYCL zero-copy path names what it cannot score instead of failing
  unnamed, crashing, or scoring wrong.** `vmaf_read_pictures_sycl()`, which
  FFmpeg's `libvmaf_sycl` filter uses on QSV frames, imports the luma plane
  only. On an Arc A380 the default model `vmaf_v1.0.16_3d0h` failed there with
  a bare `-22`; `speed_chroma_uv` needs chroma. `float_psnr_sycl` crashed
  FFmpeg. `motion_sycl` with `motion_add_uv=true` added the SAD of chroma it
  never imported (`integer_motion2_mau` 4.257894 instead of 5.536504). A CPU
  feature was dropped from the result without an error. The call now checks
  every registered extractor before it counts a frame. It returns `-ENOTSUP`
  with a libvmaf error naming each extractor that cannot run on luma alone,
  and the filter tells the user to use `hwdownload` and the `libvmaf` filter's
  `sycl_device` option ([ADR-1688](docs/adr/1688-sycl-zero-copy-luma-only-admission.md)).
  `vmaf_v0.6.1`, the filter's default model, still runs zero-copy, with the
  CPU's per-frame scores. The filter no longer prints `VMAF score: 0.000000`
  after a failed pooled score. The SYCL history and HIP upload pages no longer
  call the default model luma-only.
