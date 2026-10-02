- **The `vmaf` CLI's read-ahead checks its invariants with assertions again.**
  A lint cleanup had replaced the seven `assert()`s of the frame reader by
  early returns, so a broken invariant would have dropped a frame or ended a
  stream without a message instead of stopping a debug build. No release
  carried the change. The clang-tidy finding that prompted it is a false
  positive of clang-tidy 22 on glibc 2.44 hosts; the CI image does not report
  it.
