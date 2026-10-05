- **The FFmpeg `libvmaf_metal` filter can score NV12 and P010 VideoToolbox
  frames.** It imported only the luma plane, and libvmaf refused every frame
  with `-EINVAL`, so the filter failed on its first frame. Importing the
  chroma planes would not have helped: libvmaf copied the interleaved CbCr
  plane as if it were the Cb plane, and copied P010 samples without shifting
  them, which scores them 64 times too large. The import now reads the
  surface's pixel format. It splits NV12 and P010 chroma into Cb and Cr and
  shifts P010 to the low 10 bits. It refuses any other layout with
  `-ENOTSUP`. The filter imports all three planes of both frames. It refuses
  other software formats with an error that names the format, and fails
  instead of passing an unimportable frame through unscored
  ([ADR-1679](docs/adr/1679-metal-iosurface-biplanar-import.md)). The
  documented command now uses `-hwaccel_output_format videotoolbox_vld`;
  `videotoolbox` is not a pixel format name. Checked on Linux: host tests and
  source checks, plus a syntax check of the Objective-C++ and the filter
  against the macOS 11.3 SDK. Nothing has run on an Apple device yet. The
  macOS tester bundle runs `test_metal_iosurface_import_parity` for that.
