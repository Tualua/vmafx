---
paths:
  - .github/workflows/tests-and-quality-gates.yml
  - scripts/ci/write_cppcheck_posix_model.py
invariant: Derive cppcheck-posix-vmafx.cfg before analysis; do not pass bare --library=posix; verify null condition control.
---
# Cppcheck POSIX model correction

required Cppcheck job derives `build/cppcheck-posix-vmafx.cfg` from
installed analyzer before analysis. Preserve generator call and load
generated path, not bare `--library=posix`. Older models without
`pthread_cond_init` entry receive correct contract; newer models lose only
invalid argument-2 non-null marker because POSIX permits default attributes
as `NULL`. real-tool contract test runs after installation and keeps null
condition-object negative control, so no warning category or call site is
suppressed. See `scripts/ci/AGENTS.md` for model-shape and atomicity invariants.
