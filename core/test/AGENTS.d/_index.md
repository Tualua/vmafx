<!-- markdownlint-disable MD013 -->
# AGENTS.md — core/test

Orientation for agents working on C unit test suite. Parent:
[../AGENTS.md](../../AGENTS.md).

## Scope

C unit tests for libvmaf engine. Runs on every build via
`python3 scripts/ci/run_meson_test.py -- -C build`. Separate suite under
[dnn/](../dnn/) covers ONNX Runtime integration.

## Ground rules

- **No dead `/* ... */` blocks in test files.** Commented-out code
  that cannot compile (duplicate declarations, type-mismatched calls,
  stale APIs) must be deleted rather than left in place. If test
  scenario genuinely planned but not yet ready, add
  `// TODO(ADR-NNNN): <one-line description>` comment instead — not
  multi-line block comment containing broken code. See
  [ADR-0970](../../../docs/adr/0970-test-gpu-picture-pool-cleanup.md)
  for precedent (Round 27 audit D.4: `test_ring_buffer_threaded` dead
  block deleted).
- **Every `malloc` / `calloc` in test must be NULL-checked
  immediately.** Two idiomatic patterns accepted:
  1. *Consolidated guard* (multi-alloc SIMD tests): allocate all
     buffers, then
     `if (!a || !b || ...) { free(a); free(b); ...; return "malloc failed"; }`.
     Safe because `free(NULL)` is no-op. Reference:
     `test_integer_adm_simd.c` (L172–182), `test_vif_simd.c`
     (L163–170).
  2. *`mu_assert` guard* (loop-local single allocations):
     `mu_assert("malloc failed for X", ptr);` immediately after call.
     Reference: `test_framesync.c`, `test_pic_preallocation.c`.
  Do **not** dereference `malloc` return value before checking it.
  Unchecked dereference is latent SIGSEGV under ASan
  `MALLOC_PERTURB_=198` (ADR-0971). **Rebase-sensitive**: this rule
  applies to every new test file.
- **Parent rules** apply (see [../AGENTS.md](../../AGENTS.md)).
- **POSIX-only APIs in tests** must be shimmed for MINGW. See
  [test_lpips.c](../test_lpips.c) for `_putenv_s`-based shim for
  `setenv`/`unsetenv` — MinGW's mingw.org / MSYS2 headers do not
  expose those functions under `-std=c11 -pedantic`. CI MINGW build
  will catch this but running native test suite locally on Linux won't.
- **Never modify Netflix golden assertions**: those are Python-side, not
  here — see [../../python/test/](../../../python/test/) and
  [ADR-0024](../../../docs/adr/0024-netflix-golden-preserved.md).
- **New extractor → new test file** following `test_lpips.c` pattern:
  (a) registered by name, (b) registered by provided feature name,
  (c) options table well-formed, (d) init rejects missing required input.

- [ADR-0024](../../../docs/adr/0024-netflix-golden-preserved.md) —
  Netflix goldens (Python-side) never change.
