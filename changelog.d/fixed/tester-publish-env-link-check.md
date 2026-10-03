- **The macOS tester bundle's link check follows `@rpath` and skips install names,
  and both tester publish jobs run in the `tester-publish` environment.**
  `scripts/ci/check-macos-bundle-links.sh` resolves `@rpath` through each file's
  `LC_RPATH`, skips a dylib's own install name and accepts a reference only when it
  resolves inside the bundle or to `/usr/lib` or `/System/Library`; Tcl/Tk is no longer
  bundled. The tester image is published by dispatch on master only. See
  [the maintainer notes](docs/development/tester-image.md).
