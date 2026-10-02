- **`libvmaf_sycl` zero-copy input no longer drops `feature=` names silently.**
  With QSV-decoded frames, a `feature=` name whose extractor runs on the CPU
  (`psnr`, `ciede`, `float_ssim`, ...) used to produce no output at all, and
  several SYCL extractors dereferenced a missing picture or read stale chroma.
  Now the filter resolves every `feature=` name to its SYCL twin, and a feature
  that cannot run on zero-copy input fails the run at once with a message that
  names it (`-ENOTSUP`). On software-decoded input a feature without a SYCL
  twin is computed on the CPU with a warning, and its twin's scores equal the
  CPU extractor's. A VA surface that cannot be imported now aborts the run
  instead of skipping the frame. `scripts/test/zerocopy-e2e.sh` checks all of
  this on an Intel GPU; see
  [docs/development/sycl-zerocopy-testing.md](../../docs/development/sycl-zerocopy-testing.md).
