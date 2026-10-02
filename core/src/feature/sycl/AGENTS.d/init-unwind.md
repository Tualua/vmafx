---
paths:
  - core/src/feature/sycl/integer_motion_sycl.cpp
  - core/test/test_sycl_init_unwind.cpp
invariant: A failed extractor init owns its cleanup; calls NULL-safe local close callback.
---
<!-- markdownlint-disable MD013 MD060 -->
# Failed extractor init cleanup

- **A failed extractor `init` owns its cleanup (BUG-048 section E).** The
  generic feature-extractor framework does not invoke `close` after `init`
  returns an error. Every SYCL init path that has acquired USM, a feature-name
  dictionary, or a graph registration must therefore call its NULL-safe local
  close callback before propagating the error. This is enforced without a GPU
  by `core/test/test_sycl_init_unwind.cpp`; keep the allocator, dictionary, and
  graph fault cases when rebasing any init/close pair. Historical producer
  `709ce470e` was reverted by `5d070b0b4`; the current restoration boundary is
  documented in
  `docs/research/2101-bug048-sycl-init-unwind-restoration-2026-09-24.md`.
