- Every published archive and image now uses the strongest compression its
  documented consumers open (ADR-1591). The macOS tester bundle is
  `vmafx-tester-macos-arm64-<version>.tar.xz` (xz level 9, 27 MB instead of
  70 MB; unpack with `tar -xf`). The Windows tester zips deflate every entry at
  zlib level 9: they claimed level 9 but carried level 6, because `zipfile`
  ignores a `ZipFile`'s level for `ZipInfo` entries (2 % smaller). The release
  `models.tar.gz` and `licenses.tar.gz` and the git-archive source tarballs are
  gzip level 9, and every layer the image workflows create is gzip level 9
  (zstd would need Docker Engine 23.0 or later). The `vmaf-rc1-report` zip
  bundle deflates at level 9 too.
