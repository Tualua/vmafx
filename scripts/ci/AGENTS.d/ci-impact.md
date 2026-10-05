---
paths:
  - scripts/ci/plan-ci-impact.py
  - .github/ci-impact.json
  - scripts/ci/tests/test_ci_impact.py
invariant: Planner fails closed to `mode=full` except declared `own_paths_only`; required contexts use planner, work, gate.
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
- **Selectors `tester_image` and `windows_tester_zip` are the former trigger
  path lists of `docker-publish-tester.yml` and `windows-tester-bundle.yml`
  (ADR-1687).** Change them together with the inputs those workflows build;
  `tests/test_required_release_legs.py` pins the lists. Both workflows are
  planner consumers and therefore in `full_patterns`.
- **`own_paths_only` is the one exception to fail-closed (ADR-1700).** A selector
  declaring it is true in a full plan only when a known changed path matches its
  own patterns; with no change list (dispatch, schedule, unreadable diff) it stays
  true, which is what keeps a publish dispatch building. `load_config()` refuses
  it on a selector with `inherits` or no patterns, and `test_ci_impact.py`
  (`OwnPathsOnlyContract`) fails when a third selector declares it or when the
  property stops working. Do not widen it by adding code paths; declare it.
- **Inheritance is resolved without recursion (HISS-01).** `inheritance_order()`
  sorts the selectors topologically (and raises on a cycle) and
  `impact_selectors()` resolves them in that order in one pass.
