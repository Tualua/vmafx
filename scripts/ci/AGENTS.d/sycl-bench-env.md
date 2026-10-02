---
paths:
  - scripts/ci/sycl-bench-env.sh
  - scripts/ci/test-sycl-bench-env.sh
invariant: `$ROOT` reaches `bash -c` only as a positional argument of a single-quoted body; the test hook stays wired.
---
<!-- markdownlint-disable MD013 MD060 -->
# `sycl-bench-env.sh`: the prefix stays out of the `bash -c` body

## Workflow coupling

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `sycl-bench-env.sh` | (sourced via `eval "$(scripts/ci/sycl-bench-env.sh <version>)"` by any caller that needs side-by-side oneAPI activation; no workflow invokes it today) | `$ROOT` (from the `$ONEAPI_PREFIX` env or the version argument, both externally controlled) must stay out of any `bash -c "..."` body. It reaches the helper subshell as a positional argument: `bash -c '... source "$1/setvars.sh" ...' _ "$ROOT"`, where the body is a single-quoted literal. Interpolated, a prefix that closes the quote (`x' \|\| <payload>; false #`, or `x'$(<payload>)'`) runs arbitrary code; `set -e` blocks neither. **This fix (PR #350) was reverted once by a stale squash-merge (PR #414) and re-applied on 2026-09-19** — when resolving a conflict here, never take the double-quoted form. `test-sycl-bench-env.sh` is the gate. |
| `test-sycl-bench-env.sh` | `.pre-commit-config.yaml` — `test-sycl-bench-env` hook (pre-commit + pre-push); the required `Pre-Commit` CI job runs it with `--all-files` | Side-channel oracle: a marker file under `mktemp -d` that a hostile `$ONEAPI_PREFIX` would create, plus a check that the hostile prefix is still sourced as a literal path. It existed when the fix was reverted and would have failed (4 of 7 cases fail on the vulnerable form), but nothing ran it. Do not unwire the hook; a regression test that no gate executes protects nothing. |
