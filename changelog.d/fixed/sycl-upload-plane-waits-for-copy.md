- **`vmaf_sycl_upload_plane()` returns only after its copy has completed.**
  It used to return with the host-to-device copy still in flight. Nothing
  ordered the copy before the frame's compute, and the caller could free the
  source while the copy was still reading it. On an Arc A380, 3840x2160 frames
  uploaded with it and read at once scored the wrong pixels on every frame
  (`psnr_y` 4.9 to 6.1 where the CPU gives about 7.1). The function now waits
  for the copy, after ordering it behind the slot's previous readers, so the
  scores equal the CPU's. The Windows D3D11 import, which unmaps its staging
  texture right after the call, uses this function. The SYCL runtime's debug
  environment variables are now read through the thread-safe snapshot helper.
