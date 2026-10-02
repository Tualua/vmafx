---
paths:
  - scripts/ci/lint-configured.py
  - scripts/ci/write-compile-commands.py
  - scripts/ci/tests/test_lint_configured.py
  - scripts/ci/tests/test_write_compile_commands.py
invariant: `make lint-c` lints exactly Ninja's configured commands, every variant, `--warnings-as-errors=*`; exporter fail-closed.
---
<!-- markdownlint-disable MD013 MD060 -->
# Configured native lint (ADR-1142)

`lint-configured.py` owns local `make lint-c` selection. Make first regenerates
Meson metadata with `--reconfigure BUILD_DIR LIBVMAF_DIR`, without option
overrides, then builds generated prerequisites. Make must prepend the absolute
`VIRTUAL_ENV_ABS` to `PATH`: Meson persists its resolved Ninja command and
later launches it from the build directory, where relative `.venv/bin` is
invalid. The real-Make fixture asserts this path stays absolute. Because Meson
1.12 no longer materialises its native database, `write-compile-commands.py`
must then export
exactly Ninja's `c_COMPILER` and `cpp_COMPILER` rules. Keep that export
validated, atomic and fail-closed; never accept an empty/partial rule set or
replace a last-valid database after a failed export. Intersect the resulting
native database with tracked native sources, including engine roots, tests,
C++ tools and tracked vendored code. Preserve every configured command variant;
never infer commands for inactive backends, never regenerate database with
unfiltered `ninja -t compdb`. Only positive numeric `-flto=N` becomes `-flto`
in private analyzer copy. Keep missing/invalid inputs fatal, report excluded
scope, run cppcheck even after clang-tidy fails. Scratch-Git fixture
`tests/test_lint_configured.py` executes real Make target and both analyzer
boundaries; `tests/test_write_compile_commands.py` owns exporter failure and
last-valid-file preservation. Required Pre-Commit runs both when driver,
exporter, workflows or Makefile change.
Every configured clang-tidy command carries `--warnings-as-errors=*`. The tool
otherwise exits zero after printing ordinary findings, which turns a red
whole-tree inventory into a false-green gate. Preserve the fixture that emits
a warning with clang-tidy's default zero exit and proves the driver promotes it
to failure. Do not replace this with log parsing, a baseline, touched-file
selection, or an upstream-origin exemption.
Does not replace lane-specific ratchet measurements or their baselines.

Real-Make fixtures create failing/recording pip sentinel before fake
Meson and Ninja, satisfying recursive build dependency graph without tool
bootstrap. GNU Make does not propagate `-o` to sub-makes. Keep `PIP_NO_INDEX=1`
and assertions that no pip call, real venv or sentinel overwrite occurred;
host network access must never turn broken fixture into passing test.
