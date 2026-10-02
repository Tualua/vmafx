---
paths:
  - core/tools/meson.build
  - core/tools/vmaf.cpp
invariant: Upstream-mirror tools conform to fork lint profile; CliRunGuard invokes ordered cleanup after parsing.
---
# Orientation: CLI binary layout and lint profile

```text
tools/
  vmaf.cpp            # main() + option dispatch for the vmaf CLI (C++23, ADR-0809)
  vmaf_bench.c        # main() + benchmark harness
  vmaf_per_shot.c     # main() + scan/predict for the perShot sidecar
  cli_parse.cpp/.h    # shared option parser (--precision, --tiny-model, …)
                      # cli_parse.c is the C twin compiled into the unit/fuzz
                      # tests only; keep it byte-for-byte behaviourally in sync
                      # with cli_parse.cpp.
  vmaf_roi.c          # main() + sidecar pipeline for vmaf-roi
  vmaf_roi_core.h     # pure helpers (per-CTU mean reduce, saliency->QP)
```

## Governing ADRs

- [ADR-1155](../../../docs/adr/1155-tools-upstream-mirror-rework.md) —
  **Upstream-mirror tool TUs lint rework (0 clang-tidy warnings)**.
  `cli_parse.cpp`, `cli_parse.h`, `y4m_input.c`, `vmaf.cpp`,
  and `vmaf_bench.c` reworked to fork lint profile.
  **Rebase invariant**:
  - `cli_parse.c` was resolved as dead twin under ADR-1153 precedent
    (zero unique behavior vs `cli_parse.cpp`), deleted; `test_cli_parse`,
    `test_cli_parse_long_only_args`, and `fuzz_cli_parse` compile `cli_parse.cpp`.
    Never reintroduce `cli_parse.c`.
  - C translation units (`y4m_input.c`, `vmaf_bench.c`) MUST keep `NULL`
    (ADR-1138), suppress `modernize-use-nullptr` using file-scoped
    NOLINTBEGIN/NOLINTEND brackets to preserve MSVC `/std:clatest` Windows portability.
  - In `y4m_input.c`, all plane dimensions, strides, and buffer index
    calculations use `ptrdiff_t` / `size_t` precision to avoid 32-bit
    multiplication overflow.
  - In `vmaf.cpp`, `CliRunState` owns files, input readers, context, GPU
    handles and model arrays. `CliRunGuard` invokes one ordered cleanup path on
    every return after parsing: context, GPU handles, readers, files, CLI
    settings, then model arrays. Internal declarations live in short, reopened
    anonymous-namespace blocks so clang-tidy sees internal linkage while every
    scanner-visible block remains within 60-line HISS limit. Do not merge
    those blocks back into one file-wide namespace.
