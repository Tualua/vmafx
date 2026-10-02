---
paths:
  - scripts/ci/run_meson_test.py
  - core/test/test_meson_secret_env_sanitization.py
  - core/meson.build
invariant: Every supported Meson test entry point goes through `run_meson_test.py`; it deletes the governed credential keys first.
---
<!-- markdownlint-disable MD013 MD060 -->
# Meson parent-environment sanitization (ADR-1333)

`run_meson_test.py` must delete the governed credential keys before replacing itself with
Meson; membership and deletion may not read, retain, or print values. Keep every supported
Make, CI, preflight, bisection, setup-guidance, and Zed test entry point on this wrapper.
`core/test/test_meson_secret_env_sanitization.py` owns the exact caller inventory, mutation
coverage for each call, recursive discovery of `GNUmakefile`, `makefile`, and `Makefile`,
POSIX/Windows entry-point discovery, logical-command Meson/Ninja bypass scan, and disposable
both-log RED/GREEN proof. The scanner must normalize workflow/action `run` scalar values, quoted
or path-qualified executables and `.exe` spellings; join shell, PowerShell, and batch
continuations; and evaluate every separator-delimited command. A wrapper token may exempt only
that command, never a sibling raw test command. Keep the pre-commit filter aligned with every
scanned filename and script type. Preserve that contract with the wrapper and `core/meson.build`
default setup. Direct raw external Meson/Ninja test-target commands remain outside the bounded
guarantee.
