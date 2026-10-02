---
paths:
  - .github/workflows/tests-and-quality-gates.yml
  - docs/state.md
invariant: Enumerate tests via meson introspect; per-sanitizer exclusions track real open bugs; never widen without ADR.
---
# Sanitizer matrix test-set scope (ADR-0347)

`sanitizers` job in
[`workflows/tests-and-quality-gates.yml`](../workflows/tests-and-quality-gates.yml)
enumerates full C unit-test set via `meson introspect --tests`, then applies
per-sanitizer regex deselect:

- `address` — excludes `test_model`, `test_predict`,
  `test_float_ms_ssim_min_dim`.
- `undefined` — excludes `test_model`. Build also adds
  `-Dc_args=-fno-sanitize=function` and `cpp_args` twin to
  suppress K&R-prototype harness UB across ~50 test files
  (`core/test/test.h` callers).
- `thread` — excludes `test_model`, `test_pic_preallocation`,
  `test_framesync`. Note: `test_thread_safety_batch` is TSan-eligible
  counterpart of `test_pic_preallocation` (covers same
  threaded_extract_batch_func paths via ADR-1072/ADR-1073 without
  vmaf_preallocate_pictures), is intentionally NOT excluded.

Every deselected entry corresponds to real defect tracked in
[`../docs/state.md`](../../docs/state.md) Open-bugs. As fixes land,
corresponding `EXCLUDE='...'` regex shrinks. Never
silently widen deselect list to "make CI pass" — per
`feedback_no_test_weakening`, every addition needs ADR
referencing underlying bug. Reverting `--suite=unit` would
re-introduce zero-coverage gap (no `test()` call carries
`suite: 'unit'` tag in `core/test/meson.build`); workflow
must keep enumerating from `meson introspect --tests`.
