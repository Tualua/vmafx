---
paths:
  - core/test/test_motion_min_dim.c
  - core/test/test_lpips.c
invariant: C test files spell NULL with NOLINT bracket for MSVC; run_tests() bounded by readability-function-size at 15 branches.
---
<!-- markdownlint-disable MD013 -->
# New C test files inherit the ADR-1138 `NULL` carve-out (ADR-1166)

`core/test/*.c` compile on Windows MSVC legs with `cl.exe`, whose
documented `/std:clatest` C23 feature set does not include `nullptr`
keyword. C test files spell null pointer constant `NULL` and carry
file-scoped `NOLINTBEGIN/END(modernize-use-nullptr)` bracket citing
ADR-1138 — same shape `core/src/feature/float_motion.c` uses.
Keep closing `NOLINTEND` at EOF when appending to such file;
clang-tidy ratchet counts uncited `NOLINT` as debt (ADR-1142), so
citation comment is part of suppression, not nicety.

`run_tests()` is bounded by `readability-function-size` at 15
branches, and every `mu_run_test` expansion contributes two. Past ~7
cases, group them into named driver functions (see
`test_motion_min_dim.c`'s `run_integer_motion_tests` /
`run_float_and_metal_motion_tests`) rather than adding NOLINT.
