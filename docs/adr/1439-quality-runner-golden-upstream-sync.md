<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1439: Sync `quality_runner_test.py` golden assertions to current Netflix upstream

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `test`, `golden-data`, `upstream-sync`, `motion`, `vif`, `fork-local`

## Context

`python/test/quality_runner_test.py` holds Netflix golden assertions (GLOBAL
RULE 1). A field-by-field comparison with Netflix `upstream/master` found 33
test methods and 115 shared assertions that differ from upstream. Every
difference traces to an upstream test update the fork never ported, not to a
fork code defect:

- Netflix `a44e5e611` (integer motion edge-mirroring fix) moved `motion2` on
  every input and the pooled `VMAF_score` values built on it (for example
  `src01` `VMAF_score` 76.66890519623612 → 76.66783025).
- Netflix `142c06714` (float VIF on-the-fly kernel) moved the four float
  `vif_scaleN` scores and every pooled, bootstrap, bagging and ensemble score
  derived from them.

The fork already carries the code for both changes, but each stale value was
kept passing by loosening `places` (down to `places=1`), which hides the real
value and stops the assertion from catching a regression. Syncing a golden to
Netflix's own current value is the opposite of editing a golden to mask a code
defect, and was approved by the product owner.

## Decision

Every assertion that exists in both files and differs from `upstream/master`
takes upstream's exact value literal and `places`, copied from upstream's
source text. The rewrite is mechanical (AST positions of the value and
`places` arguments only; formatting and fork-only assertions untouched). After
the sync no shared assertion differs from upstream. The full
`quality_runner_test.py` suite is the arbiter.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| **Sync every shared assertion to upstream (chosen)** | Golden equals current Netflix; `places=4` restored; regressions caught again | 115 assertions in one change | — |
| Sync only the `src01` motion / VMAF values | Smaller change | Float-VIF-derived scores stay loose and inconsistent with each other | Leaves the same masking elsewhere |
| Keep the loosened `places` | No golden change | Assertions that cannot fail; contradicts GLOBAL RULE 1 | Rejected |

## Consequences

- **Positive**: the golden file is Netflix's own current file for every shared
  assertion again; a future CPU regression fails at `places=4`.
- **Negative**: none measured. With the synced file, the full
  `quality_runner_test.py` passes on this branch's code built with GCC
  (61 passed, 1 skipped) and with `icx`/`icpx` (61 passed, 1 skipped).
- **Neutral / follow-ups**: five upstream test methods have no fork
  counterpart yet and are out of scope. Fork-only assertions are unchanged.

## References

- req: product-owner approval of the golden sync, 2026-07-09 (Option A, "full
  sync of the whole file"); 2026-10-01 popup: port the work onto a new branch
  from master.
- Netflix `a44e5e611`, `142c06714`.
- Research digest: [`docs/research/1439-quality-runner-golden-upstream-sync.md`](../research/1439-quality-runner-golden-upstream-sync.md).
