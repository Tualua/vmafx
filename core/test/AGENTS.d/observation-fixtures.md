---
paths:
  - core/test/test_svm_parser.c
  - core/test/test_speed.c
  - core/test/test_speed_qa.c
  - core/test/test_iqa_convolve_coverage.c
  - core/test/test_integer_motion_v2_coverage.c
  - core/test/test_ssim_coverage.c
  - core/test/test_integer_psnr_coverage.c
invariant: Observation test fixtures keep original registrations and order; parser and input arrays remain read-only.
---
<!-- markdownlint-disable MD013 -->
# Observation-only fixtures: SVM, SpEED, IQA, and metric coverage

## Observation-only SVM test cleanup (Research-2049)

`test_svm_parser.c` keeps nine malformed-model fixtures and their
order; its header-size/header-order driver helpers propagate first
failure without adding test count. Parser model views and runtime
API query arrays are read-only. Preserve all assertions, public
`svm.h` calls and ownership teardown; these tests do not justify
changes to vendored `svm.cpp` or its header. See
[Research-2049](../../../docs/research/2049-svm-observation-test-lint-2026-09-08.md).

## SpEED test fixture grouping (Research-2050)

`test_speed.c` and `test_speed_qa.c` keep their original five
registrations apiece, assertion expressions/messages, input
literals and API call order. Temporal SpEED-QA setup uses
`alloc_temporal_pictures` and `init_temporal_extractor` to remain
below strict branch limit; each caller must immediately return
helper's failure message. These helpers are setup stages, not
additional registered tests. Descriptor views are const because
context-creation API already accepts read-only descriptors. Preserve
ADR-1138 C `NULL` bracket and measured zero warning baseline. See
[Research-2050](../../../docs/research/2050-speed-test-native-lint.md).

## IQA/motion observation fixtures (Research-2053)

`test_iqa_convolve_coverage.c` keeps seven input-only image arrays
const; `iqa_img_filter` inputs and kernel storage remain writable.
Its boundary-test group preserves first five cases, propagates
failure immediately and does not increment test count itself. Five
edge-16 motion source arrays are read-only; all expected sums and
mirror fixtures remain unchanged. See
[Research-2053](../../../docs/research/2053-observation-fixture-const-2026-09-08.md).

## Metric coverage setup stages (Research-2054)

`test_integer_motion_v2_coverage.c`, `test_ssim_coverage.c` and
`test_integer_psnr_coverage.c` retain all nineteen registrations and
their order. Their private setup helpers preserve descriptor
lookup, option insertion, context creation/init, collector creation
and every assertion in order; callers immediately propagate first
failure. Keep existing ownership and teardown behavior, const
descriptor views and C NULL brackets. Helpers are not new
registered cases. See
[Research-2054](../../../docs/research/2054-metric-coverage-const-2026-09-08.md).
