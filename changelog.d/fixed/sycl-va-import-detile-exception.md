- **A SYCL error while de-tiling a VA surface no longer ends the process.**
  `vmaf_sycl_import_va_surface()` submitted its de-tile copy or kernel outside
  any `try`, so a synchronous `sycl::exception` (a kernel the device cannot
  build, an allocation the runtime cannot make) left an `extern "C"` function
  and terminated the program. The submit is caught now: the import is
  released and the call returns `-EIO`, as the readback path already did.
