---
paths:
  - core/test/test_sycl_init_unwind.cpp
  - core/test/test_sycl_cuda_serial_upload_lifetime.c
invariant: test_sycl_init_unwind is device-free GNU-ld interposer; test_sycl_cuda_serial_upload_lifetime needs CUDA and SYCL.
---
<!-- markdownlint-disable MD013 -->
# SYCL init unwind and upload lifetime

- **`test_sycl_init_unwind` is device-free GNU-ld interposer.** Keep it
  Linux-only, statically linked, and disabled when `b_lto=true`: LLVM LTO
  resolves libvmaf's internal allocator/dictionary/graph calls before
  `--wrap` can rewrite them. It must remain in `fast` + `sycl` suites and
  cover all descriptors listed in
  `docs/research/2101-bug048-sycl-init-unwind-restoration-2026-09-24.md`; adding
  SYCL init that owns USM requires adding its failure case here.

## Combined CUDA+SYCL upload lifetime (BUG-040)

`test_sycl_cuda_serial_upload_lifetime.c` is registered only when both
`enable_cuda` and `enable_sycl` are true. That compile combination is
load-bearing: CUDA's host-picture cleanup has early return which must not
bypass SYCL upload wait. test uses public `vmaf_read_pictures()`
path with `psnr_sycl` at both `n_threads=0` and `n_threads=1`; private release
callback poisons each 4K distorted plane immediately when its final reference
drops. Identical input must remain at 8-bit 60 dB cap for all 48 frames,
proving upload finished before callback on both ownership paths. It
needs SYCL device but no CUDA device and skips cleanly when SYCL
initialization is unavailable. Keep it in `slow`, `gpu`, and `sycl`
suites.
