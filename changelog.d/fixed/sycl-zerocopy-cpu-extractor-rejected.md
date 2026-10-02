- **`vmaf_read_pictures_sycl` no longer drops CPU extractors on zero-copy
  input.** With QSV-decoded frames the `libvmaf_sycl` filter hands the
  extractors no host pictures; a CPU extractor registered through
  `feature=name=psnr` or `name=cambi` was skipped without a message and its
  scores were missing from the output. The call now returns `-ENOTSUP` before
  any state changes and logs the extractor and its SYCL twin, so the FFmpeg run
  fails instead of reporting an incomplete result. Models are unaffected: their
  features already resolve to SYCL twins. See
  [ADR-1595](../../docs/adr/1595-sycl-zerocopy-fail-loud-twin-routing.md) and
  [SYCL zero-copy testing](../../docs/development/sycl-zerocopy-testing.md).
