---
paths:
  - core/test/meson.build
  - core/test/test_gpu_serialization_contract.py
invariant: Every test() in meson.build MUST carry suite: argument; every test carrying gpu suite tag MUST set is_parallel : false.
---
<!-- markdownlint-disable MD013 -->
# Suite-tagging invariant

**Every `test()` declaration in [`meson.build`](../meson.build) MUST
carry `suite:` argument.** `fast` suite is documented pre-push gate
(`CLAUDE.md §3`; `python3 scripts/ci/run_meson_test.py -- -C build --suite=fast`) and must
contain every test that completes in under 2 seconds under normal
CPU load.

**Every test carrying `gpu` suite tag MUST also set
`is_parallel : false`.** GPU-suite tests share physical accelerator and
its finite queue/memory resources. Meson defines `is_parallel : false` as
exclusive test: it waits for all running tests before starting it and starts no
other test until it completes. `check_gpu_test_serialization.py` enforces this
contract from Meson's public `intro-tests.json` metadata for every configured
backend. `test_gpu_serialization_contract.py` also scans source registry so
dormant backend (notably Metal on Linux) cannot evade configured-metadata
check. Keep both guards registered outside `gpu` suite so they can inspect
complete test set without making themselves accelerator tests.

Tag assignments:

| Suite tag(s)          | Criteria                                                  |
|-----------------------|-----------------------------------------------------------|
| `['fast']`            | CPU-only unit test, finishes in <2 s                      |
| `['fast', 'simd']`    | SIMD bit-exactness test, arch-gated, finishes in <2 s     |
| `['fast', 'gpu']`     | GPU backend scaffold/contract smoke, finishes in <2 s     |
| `['slow']`            | Runs longer than 2 s (e.g. `test_mcp_smoke`, timeout 60s) |

**Rebase-sensitive**: upstream Netflix/vmaf may add new `test()`
calls without `suite:` arguments when cherry-picking or syncing.
After every upstream sync or port-upstream-commit, run:

```bash
grep "^test(" core/test/meson.build | grep -v "suite :"
```

Any line returned is violation — add appropriate `suite:` before
merging. Keep this check with every upstream sync because upstream does not
carry fork's suite classification contract. Then run both
source-registry and configured-metadata guards:

```bash
python3 scripts/ci/run_meson_test.py -- -C build --no-rebuild \
  test_gpu_serialization_contract check_gpu_test_serialization
```

Together they catch new `gpu` registrations that omitted exclusive
scheduling flag, including dormant backend and combined-backend suite lists
such as `['slow', 'gpu', 'sycl']`.

## Governing ADRs

- [ADR-0015](../../../docs/adr/0015-ci-matrix-asan-ubsan-tsan.md) —
  sanitizer matrix (tests run under ASan + UBSan + TSan).
- [ADR-0347](../../../docs/adr/0347-sanitizer-matrix-test-scope.md) —
  sanitizer matrix test-set scope. **Rebase-sensitive invariant**:
  sanitizer job in
  `.github/workflows/tests-and-quality-gates.yml` enumerates full
  unit-test set via `meson introspect --tests` and applies per-sanitizer
  deselect regex (ASan / UBSan / TSan each have own list). When
  adding new `test()` call to [`meson.build`](../meson.build), test
  inherits sanitizer coverage automatically. Do NOT add
  `suite: 'unit'` tag to any `test()` call without coordinating
  with ADR-0347. Workflow no longer relies on `--suite=unit` (which
  previously matched zero tests because no `test()` carried
  tag); partial tagging would silently re-introduce gap. Under
  UBSan, build adds `-fno-sanitize=function` to suppress
  K&R-prototype harness UB across every `test_*.c`; new test files
  should follow existing `static char *test_X()` pattern for
  upstream-parity. Future T7-5-style sweep PR that converts every
  test function to `(void)` parameters must also drop
  `-fno-sanitize=function` from workflow in same PR.

## Test timeouts and readiness invariants

- **Timeouts are evidence-based**: test timeouts in `meson.build`
  must reflect measured execution distributions. Timeout may change
  ONLY with measured evidence of why passing case needs it. If test
  times out, investigate root cause (deadlock, socket accept hang,
  blocking I/O) rather than reflexively raising timeout.
- **Readiness is polled, never slept**. Applies when testing
  asynchronous servers or worker threads (such as stdio, UDS, or
  SSE MCP transports). Synchronize on real readiness signals, or
  poll readiness endpoints with timeout, rather than using fixed
  `sleep()` calls.
