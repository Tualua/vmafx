---
paths:
  - core/test/test_pelorus_interop.c
  - scripts/sync-pelorus-interop.sh
invariant: test_pelorus_interop.c is exact Pelorus fixture; never add VMAFx-only NOLINT bands, (void) casts, or warning repairs.
---
<!-- markdownlint-disable MD013 -->
# Pelorus exact-source conformance fixture (ADR-1113, ADR-1276)

`test_pelorus_interop.c` is not fork-authored test implementation. From its
first vendored `#include` onward, it is exact Pelorus fixture body at
`PELORUS_VENDOR_SHA`, with only `pelorus/` to `libvmaf/pelorus/` include
rewrite. Keep every test, cast, return-value expression, and formatting choice
identical to that source.

- Never add VMAFx-only `NOLINT` bands, `(void)` casts, formatting changes, or
  warning repairs to this file. Fix real defect in Pelorus, publish/review
  source commit, then re-vendor it.
- Put VMAFx lint and format policy in `.pre-commit-config.yaml`, Makefile,
  and `scripts/ci/tidy-ratchet.py`. exact-source fixture is excluded there.
- After any re-pin, run `scripts/sync-pelorus-interop.sh` against Git checkout
  containing exact object, then run `test_pelorus_interop` under normal,
  ASan, and UBSan builds. default guard renders canonical VMAFx prefix
  plus transformed Pelorus body and compares complete file byte-for-byte;
  it fails closed when object is unavailable or prefix is changed.
