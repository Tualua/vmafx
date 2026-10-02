---
paths:
  - core/test/fuzz/README.md
  - core/test/fuzz/meson.build
  - core/test/fuzz/fuzz_*.c
invariant: fuzz/ subdir holds libFuzzer harnesses for parser surfaces; read_json_model parser fix must land in both .c and .cpp.
---
<!-- markdownlint-disable MD013 -->
# libFuzzer harnesses (`fuzz/`)

[`fuzz/`](../fuzz/) subdir holds libFuzzer harnesses for parser
surfaces (ADR-0270 scaffold; ADR-0311 expansion;
[ADR-0882](../../../docs/adr/0882-fuzz-target-audit-json-model-sidecar.md)
json_model + dnn_sidecar additions). Conventions:

- Each harness binds **one** public parser entry point via
  `LLVMFuzzerTestOneInput(const uint8_t *, size_t)`. Harnesses
  that need `FILE *` use `fmemopen`; path-based loaders use
  per-process `/tmp/vmaf-fuzz-<target>-<pid>` tempfile reused
  across iterations (see `fuzz_dnn_sidecar.c` for pattern).
- Internal (non-`VMAF_EXPORT`) entry points cannot be reached
  through `libvmaf.so` because
  [ADR-0379](../../../docs/adr/0379-libvmaf-symbol-visibility.md)
  builds shared library with `-fvisibility=hidden`. Mirror
  precedent in `test_model` / `test_model_loader` and compile
  relevant source files directly into harness binary (e.g.
  `fuzz_json_model` pulls `core/src/read_json_model.c` +
  `pdjson.c` + `dict.c` + `log.c`).
- Seed corpora under `<target>_corpus/` are committed verbatim
  and kept small (one per branch class). Known-crash reproducers
  go under `<target>_known_crashes/` and are **excluded** from
  nightly CI seed path; they exist so regression catches moment
  underlying fix lands.
- Per [ADR-0404](../../../docs/adr/0404-nightly-fuzz-triage-keep-gates.md),
  harness that surfaces real bug stays on in CI without
  `continue-on-error` until fix lands. Document finding in
  `docs/state.md` and link reproducer from `README.md`.
- Fuzz build requires clang + `-Db_lto=false` when any harness
  pulls libvmaf-internal sources (ASan + LTO discards
  module-dtor sections at link time on larger source sets). See
  build recipe at top of `fuzz/README.md`.
  - **Ported assertion is measured against merge base, never
    against baseline you regenerated afterwards.** When ADR-1153
    makes you port dead twin's unique coverage into live side before
    deleting it. Twin's idioms come with it. If twin was C and live
    side is C++, every `NULL`, `typedef struct`, `{0}` sentinel and
    file-scope `static` is fresh clang-tidy warning. Run
    `python3 scripts/ci/tidy-ratchet.py --lane cpu --build-dir build`
    against branch's **merge base** and again after port, compare
    two. Regenerating baseline after port makes any increase
    invisible: that is how PR #1219 took `core/test/test_feature.cpp`
    from 9 warnings to 34 without gate firing. Translate idioms as
    you port — C++ TUs use `nullptr` (ADR-1138's `NULL` rule is
    scoped to **C** TUs, for MSVC `/std:clatest`), plain `struct`,
    `{}` sentinels, and anonymous namespace instead of file-scope
    `static`.
  - **Free before you assert.** `mu_assert` expands to early
    `return message`, so any assertion evaluated while heap pointer
    live leaks that pointer on failure path. Compute comparison into
    `const bool`, free, then assert on bool — null-guard comparison
    so `nullptr` return fails assertion instead of faulting inside
    `strcmp`. `clang-analyzer-unix.Malloc` only reports this once
    function is small enough for it to analyse fully, so oversized
    test function hides leak rather than avoiding it.
  - **`read_json_model` is twin pair, and fuzz harness uses `.c`
    side.** Library builds `core/src/read_json_model.cpp`;
    libFuzzer target compiles `core/src/read_json_model.c` directly
    (`core/test/fuzz/meson.build`). Pair is not in
    `scripts/ci/twin-drift-allowlist.txt`, so parser fix must land
    in **both** files. Fixing one and testing other is real trap:
    library-linked reproducer will report bug fixed while fuzz lane
    stays red, because two binaries do not share that translation
    unit. `scripts/ci/twin-drift-check.sh` labels `.c` "test-only
    twin side".
