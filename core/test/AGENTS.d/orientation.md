---
paths:
  - core/test/test.h
  - core/test/mu_table.h
  - core/test/meson.build
invariant: Tests follow test.h; readability-function-size 15 branches, HISS-04 60-line cap; clean up before asserting.
---
<!-- markdownlint-disable MD013 -->
# Test style

All tests follow trivial µnit-style pattern declared in
[test.h](../test.h):

```c
static char *test_some_invariant(void)
{
    mu_assert("description", predicate);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_some_invariant);
    return NULL;
}
```

Each `test_*.c` compiles into own binary. `meson.build` registers them
with repository's Meson test runner. No fixtures, no shared state — each test owns setup
and teardown.

**Function size** (`readability-function-size`, 15-branch budget):

- `mu_assert` / `mu_run_test` = 2 branches each (`if` + `do { } while (0)`) -> more than 7 in one function fails.
- more than 7 tests -> `MU_TEST(fn)` rows + `mu_run_table()` from [mu_table.h](../mu_table.h). 0 branches at any length.
- assertion-heavy test -> `check_*` helpers, called as `char *msg = check_x(...); if (msg) return msg;` = 1 branch.
- `mu_assert_msg(check_x(...))` from [test.h](../test.h) is same propagation in one line. Use it; do not re-declare local copy.
- SYCL parity test pipelines -> split linear setup, frame-feed loop, and score collection into cohesive phase helpers (`setup_*`, `feed_*_frames`, `collect_*_scores`) returning `mu_message_t` and propagated via `mu_assert_msg()`. Preserves verbatim assertion strings and numeric checks without exceeding BranchThreshold 15 (T-SYCL-RATCHET-TEST-BRANCH-COUNT-2026-09-22).

**Block length** (HISS-04, `praetorctl audit`, 60-line hard cap):

- cap counts every brace block at file scope, function or not: anonymous `namespace`, table initializer, `switch` body.
- C++ TU -> several short `namespace { ... } // namespace` blocks, one per cohesive group. Same internal linkage, no suppression. Same shape as SYCL extractors and `core/tools/vmaf.cpp`.
- long `MuTest` table -> several short tables + one `mu_run_table()` call each, in order. Order and first-failure behaviour unchanged.
- split test body -> phase helper returning `mu_message_t`, never per-block partial sums. Assertion strings and expected values stay byte-identical.
- HIP parity test -> `hip_parity_skip()` from [hip_parity_skip.h](../hip_parity_skip.h) for `-ENOSYS` scaffold teardown. One definition; twelve tests share it.
- **do not split** body whose `NOLINT` cites external contract fork does not own (ADR-1286). Two here: `ref_calc_psnrhvs()` in [test_psnr_hvs_simd.c](../test_psnr_hvs_simd.c) (ADR-0138 bit-exactness) and `test_barten_csf()` in [test_barten_csf.c](../test_barten_csf.c) (upstream's `mu_assert` sequence verbatim; upstream still appends cases, so reshaping it turns every later sync into hand-merge). Both keep their cited suppression and stay over cap.

**Clean up before asserting:**

- failing `mu_assert` returns at once -> live heap, `FILE`, temp file or changed env var leaks.
- free, close, remove, restore env first; keep result in local; assert last.
- `VMAF_TINY_MODEL_DIR` left set -> every later test in binary breaks.
- Tidy Changed analyzer reports these paths (`clang-analyzer-unix.Malloc`, `clang-analyzer-unix.Stream`).
