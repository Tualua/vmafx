- **The cambi and psnr_hvs HIP host code and the cambi device header are
  clean under clang-tidy (ADR-1142).** The `hip` lint lane is configured
  without hipcc and therefore analyses the `-ENOSYS` stubs of the HIP host
  files, not the bodies a device runs. A hipcc build showed 35 findings in
  those bodies: `integer_cambi_hip.c` (24) now binds its device arena through
  one accessor instead of sixteen casts through `void *`, and
  `integer_psnr_hvs_hip.c` (11) passes its kernel arguments with explicit
  conversions. `integer_cambi/cambi_hip_device.h` widens eleven row and
  column offsets before the multiplication (findings the lane sees and its
  baseline did not record). No behaviour change: every HIP twin returns the
  same values as before on a gfx1036 (17 800 of 17 800 values of the sweep).
