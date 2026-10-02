---
paths:
  - core/test/test_picture.c
  - core/test/test_picture_v2.c
  - core/test/test_picture_pool_error_paths.c
  - core/test/test_predict.c
  - core/test/test_predict_source_authority.py
  - core/test/test_predict_nonfinite_log_output.py
  - core/test/test_feature_collector.c
  - core/test/test_pdjson.c
  - core/test/test_pdjson_stack_increment.c
  - core/test/test_thread_pool_backpressure.c
invariant: Do not add uncalled library sources to test executables; all private-source test binaries use predict_test_dependencies.
---
<!-- markdownlint-disable MD013 -->
# Test target source identity contracts (ADR-1142, Research-2096)

Do not add uncalled library implementation sources directly to test executables in
`core/test/meson.build`. `test_picture*` must not compile `thread_pool.c`, and
`test_predict` / `test_model*` must not compile redundant `pdjson.c` copies.
`test_picture`, `test_picture_v2`, and `test_picture_pool_error_paths` link
test-local static library `test_picture_impl` rather than compiling duplicate
private copies of `picture.c`, `mem.cpp`, and `ref.cpp`. error-path target
still compiles `picture_pool.c` directly; preserve that ADR-0960 seam while
keeping shared picture implementation identity.

`test_predict.c` includes `predict_internal.h` for pure mapping/equality
helpers and links production predictor; it must never text-include
`predict.c`. unity include creates second set of static helpers whose calls
can attach to CodeQL's coalesced production identity, orphaning duplicate
graph. `test_predict_source_authority.py` locks that build boundary, while
`test_predict_nonfinite_log_output.py` runs linked binary and verifies
real production warning is emitted exactly once.

All private-source test binaries use `predict_test_dependencies`, which links
one `predict_c_lib` object compiled in `core/src/meson.build`; no test source
list may compile `../src/predict.c`. Keep source-authority test's global
Meson assertions. former per-target pattern produced 52 redundant test
objects plus library object and left CodeQL with orphan scan graph.

`test_feature_collector` text-includes `libvmaf.c` for private collector state,
but it must not compile `predict.c`. Keep `vmaf_cflags_common` and
`predict_test_dependencies` on target: linked source-authority archive
satisfies included file's predictor references without creating another
implementation identity.

When test must compile implementation source under special configuration,
its repeated definitions need unambiguous test-local identities. pdjson
default, zero-increment, and oversized-increment copies are separate static
libraries. Their private helpers receive target-unique names while public
`json_*` API stays unchanged; each executable separately renames `run_tests` so
its test body retains distinct root. backpressure test renames four
`vmaf_thread_pool_*` entry points around its intentional `thread_pool.c`
inclusion. Keep definitions and test calls under same aliases; never export
aliases from production headers or replace them with scanner suppressions.

Configuration-only helpers must be emitted only in configuration that uses
them. In particular, `vector_unchanged` stays inside `FEX_VECTOR_ALLOC_TEST`; do
not restore `[[maybe_unused]]` or add unrelated unconditional test merely to
manufacture analyzer reachability. two specialized pdjson growth tests remain
separate binaries and retain their focused invalid-increment assertions.

CodeQL closure is proved by replaying `UnusedStaticFunctions.ql` against fresh
full-build database and then confirmed by hosted default-branch run. Runtime
coverage alone cannot prove this repeated-compilation identity contract.
