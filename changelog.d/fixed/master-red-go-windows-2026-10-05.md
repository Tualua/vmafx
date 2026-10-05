- **The `Go` workflow builds the node's eBPF object with the pinned clang again,
  and the Windows SYCL tester leg no longer fails on a locked installer.**
  `scripts/dev/gen-node-bpf.sh` now prefers `clang-19` (and `llvm-strip-19`) to a
  newer default `clang`, which the hosted runner ships. The oneAPI install step of
  `windows-tester-bundle.yml` checks the extractor's exit code and retries the
  removal of the installer a bounded number of times. No score, public API or
  FFmpeg patch impact.
