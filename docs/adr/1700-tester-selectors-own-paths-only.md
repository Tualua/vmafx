<!-- markdownlint-disable MD013 MD060 -->
# ADR-1700: The tester selectors follow their own paths, not the full-mode fallback

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: lusoris
- **Tags**: `ci`, `release`, `docker`, `testing`

## Context

[ADR-1687](1687-required-release-dry-run-legs.md) made `Tester Image` and
`Windows Tester Zip` required contexts and moved the two tester workflows onto
the impact planner ([ADR-1140](1140-ci-impact-planner.md)): selectors
`tester_image` and `windows_tester_zip` hold the path lists the workflows'
triggers used to carry. The planner fails closed: when it cannot bound a change
(anything under `scripts/ci/`, a workflow that hosts a required context,
`.standards-baseline.json`, `Makefile`, a delete or rename, an unknown path) it
plans `mode=full` and sets every selector, these two included.

ADR-1687 measured what that costs, over the 200 first-parent master commits
`374e4342a..8e60965f0` (ADR-1687 names the newest one `3d9f162bd`, the commit
before it): 142 full plans; the tester image would start on 148 of the 200 and
the Windows zip on 142, against 41 and 10 under the former path filters. At the
measured medians (47 and 37 minutes) that is about 7000 instead of 1900
tester-image minutes and 5300 instead of 370 Windows minutes per 200 pull
requests, on a queue that is already saturated. A CI-authority change buys
nothing in those builds: the image and the zip read three install scripts and the
two build scripts of `scripts/ci/`, and those are in the selectors.

The maintainer decided (2026-10-05) that the two selectors fire on their own
input paths only, as a declared, narrow exception to the fallback, and that a
master push keeps waiting for the push legs as ADR-1687 describes.

## Decision

We will let a selector declare `"own_paths_only": true` in
`.github/ci-impact.json`, and declare it on `tester_image` and
`windows_tester_zip` only:

- In a full plan whose changed paths are known (a CI-authority file, a delete or
  rename, an unknown path, an empty diff), such a selector is true only when one
  of those paths matches its own patterns; the fallback never sets it by itself.
  Both workflow files are in their own selector, so editing a workflow still runs
  it.
- When the planner has no change list (an event it does not route, such as a
  dispatch or a schedule, or a diff it could not read), the selector stays true:
  a publish dispatch and a scheduled run always build.
- `load_config()` refuses the property on a selector with `inherits` or without
  patterns, and on a value that is not a boolean.
  `scripts/ci/tests/test_ci_impact.py` fails when a selector other than the two
  declares it, and plants the mutation (property removed) that would start the
  builds on a `scripts/ci/` change.

On the 200 commits above, the two selectors now select exactly the commits the
former path filters selected (41 and 10, commit for commit). Selector inheritance
is resolved in topological order instead of by recursion, which removes the
planner's baselined HISS-01 finding. The master-push behaviour of ADR-1687 is
unchanged: a push that selects a tester build makes the master aggregator wait
for every leg the push run builds.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the fallback for these selectors (ADR-1687 as landed) | No exception in the planner contract | The builds start on about three quarters of all pull requests, 3.6 and 14 times the former runner time | The extra builds test nothing a CI-authority change can break |
| Special-case the two selector names in `plan-ci-impact.py` | One line of code | An exception nobody sees in the map; the next selector that wants it copies the special case | The property is declared where the selectors are, and the tests bound who may use it |
| Restore trigger `paths:` on the two workflows and drop them from the aggregator | The former behaviour exactly | Gives up the required contexts ADR-1687 decided | Reverses the decision instead of refining it |

## Consequences

- **Positive**: the tester builds run on the pull requests that touch their
  inputs and nowhere else; the required contexts stay; a dispatch and the nightly
  schedule still build.
- **Negative**: the planner contract has one declared exception to fail-closed. A
  change to a `scripts/ci/` file the image reads that is not in the selector
  (`check-msvc-clz-shim.sh`, `cross_backend_parity_gate.py`, `exact_twins.d/`) is
  first built by the push run or the nightly build of
  [ADR-1701](1701-nightly-tester-image-build.md).
- **Neutral / follow-ups**: `docs/development/ci.md` and
  `docs/development/release-workflow-verification.md` describe the property and
  the master wait.

## References

- `Q2.1` (popup, 2026-10-05): "Own paths only (Recommended)".
- `Q2.3` (popup, 2026-10-05): "Yes, master waits (Recommended)".
- [ADR-1687](1687-required-release-dry-run-legs.md) (refined), [ADR-1140](1140-ci-impact-planner.md),
  [ADR-1701](1701-nightly-tester-image-build.md).
