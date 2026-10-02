---
paths:
  - scripts/ci/plan-ci-impact.py
  - .github/ci-impact.json
  - scripts/ci/tests/test_ci_impact.py
invariant: The planner fails closed to `mode=full`; required contexts use planner -> work -> gate, never trigger `paths:` filters.
---
<!-- markdownlint-disable MD013 MD060 -->
# CI impact planner (ADR-1140)

- `plan-ci-impact.py` + `.github/ci-impact.json` decide which surfaces change
  touches; every required job runs it first, gates heavy steps on
  selectors. **Fail-closed**: unknown top-level paths, non-additive
  statuses (delete/rename/copy), CI-authority files (this directory included),
  missing merge-base, non-linear pushes and over-large diffs all yield
  `mode=full`.
- `tests/test_ci_impact.py` (stdlib `unittest`) pins map ↔ tree contract and
  no-path-filter invariant on required-context workflows. Run it after
  adding top-level directory or required check.
- **Required contexts use planner -> work -> gate, never trigger filters
  (BUG-098).** The workflow always starts. An unconditional `impact` job exports
  one selector; distinctly named heavy `... work` jobs consume it; and an
  `if: always()` gate alone owns each exact required context name. The gate may
  accept only `true:success` or `false:skipped` and must fail when planning fails.
  Keep the `(?m)` multiline anchor in the no-path-filter regression: omitting it
  makes the assertion inspect only the beginning of the YAML and silently miss
  every nested `paths:` key. GitHub does not create the gate check until its
  `needs` chain completes, so the required aggregator must keep polling while a
  mapped planner/work proxy is active and briefly after it completes. Preserve
  the complete `delayedStrictDependencies` map and paginated check-run fetch;
  `test_hiss_replay_contract.py` executes both failure modes.
