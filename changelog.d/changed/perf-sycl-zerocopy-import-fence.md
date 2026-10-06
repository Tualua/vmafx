- **SYCL zero-copy: the `libvmaf_sycl` filter no longer waits on the host at the start of a QSV frame.**
  The VA import now orders its writes to the upload slot after that slot's
  previous readers on the device, across every SYCL queue, including an
  extractor `n_subsample` skipped. The filter keeps
  `vmaf_sycl_wait_compute()` on its host-upload path only, so the import of
  a frame can overlap the compute of the previous one. Scores do not change;
  the throughput gain on an Arc A380 is measured separately
  ([ADR-1769](docs/adr/1769-sycl-zerocopy-throughput-a380.md)).
