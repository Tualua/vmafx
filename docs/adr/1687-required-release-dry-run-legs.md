<!-- markdownlint-disable MD013 MD060 -->
# ADR-1687: Require the pull-request release legs through the aggregator

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: lusoris
- **Tags**: `ci`, `release`, `docker`, `supply-chain`, `testing`

## Context

[ADR-1595](1595-pr-time-verify-push-only-workflows.md) gave three workflows a
pull-request run: the amd64 tester image (`docker-publish-tester.yml`), the x64
Windows tester zip (`windows-tester-bundle.yml`) and `release-dry-run.yml` (the
release images, the `vmaf-mcp` distribution and its SBOM, nothing published). It
left them out of `required-aggregator.yml` until their runtime was known, so a
red leg did not block a merge. The maintainer measured the last 15 completed
pull-request runs of each on 2026-10-05:

| Workflow | Median | p90 |
| --- | --- | --- |
| Tester image (amd64 build and test) | 47 min | 107 min |
| Windows zip (x64 build, verify, SBOM) | 37 min | not recorded |
| Release dry run | 7 min | 25 min |

and decided that all three become required: the two tester legs when the pull
request touches their inputs, the dry run on every pull request.

The aggregator contract constrains how. A context in the `required` array that
never reports is accepted ([ADR-0313](0313-ci-required-checks-aggregator.md)),
which also accepts a workflow that failed to start. A workflow that hosts a
required context may not filter its triggers by path; it starts on every pull
request and master push and routes in-job through `scripts/ci/plan-ci-impact.py`,
with an `if: always()` gate owning the exact context name, and those gates are
`strictMustReport` ([ADR-1140](1140-ci-impact-planner.md), BUG-098,
[ADR-1297](1297-ci-gate-every-reporting-check.md)). `scripts/ci/tests/test_ci_impact.py`
fails on a trigger `paths:` list in such a workflow. Both tester workflows had one;
the dry run already started on every pull request and routed its three groups
through `scripts/ci/release-dry-run-plan.sh`, but it has no push trigger.

## Decision

We will make `Tester Image`, `Windows Tester Zip` and `Release Dry Run` required
contexts, all three in `strictMustReport`:

- **Tester image and Windows zip** follow the BUG-098 shape. The trigger `paths:`
  lists go; job `impact` runs the planner, and the existing jobs start only when
  its new selector is set: `tester_image` and `windows_tester_zip` in
  `.github/ci-impact.json`, whose patterns are the former path lists exactly. The
  gate needs the last pull-request job of each chain: `build` for the image (it
  needs `validate` and `refs-x86`, so it is skipped when either fails) and
  `verify` for the zip (it downloads what the last step of `build` uploads). It
  passes `selected=true` with that job's success and `selected=false` with its
  skip, and fails otherwise. Both workflows join the planner's `full_patterns`,
  like every other planner consumer. Their `validate` jobs are renamed
  (`Validate tester image source`, `Validate Windows zip source`) so the names the
  aggregator waits on while a gate is unregistered are unique.
- **Release dry run** keeps `scripts/ci/release-dry-run-plan.sh`. A gate job
  `Release Dry Run` needs the plan and the three groups and passes each group as
  selected and successful, or unselected and skipped. The workflow has no push
  trigger, so a new aggregator list `pullRequestOnly` removes the context from a
  run that is not a pull request, the way the aggregator already removes the
  Scorecard gate of the other event.
- `delayedStrictDependencies` lists, per gate, the job names GitHub reports
  before the gate exists, matrix legs expanded (checked against runs 37276278197,
  37276297280, 37271628250, 37271658228 and 37276278301).

The publish jobs and the push-only legs (arm64 image, GPU images, arm64 / CUDA /
SYCL zips) are not pull-request legs and are not required on a pull request.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Require the dry run only | Seven minutes per pull request; no change to the tester workflows | The tester image and Windows zip stay red-but-mergeable, the gap ADR-1297 closes for every other reporting check | Maintainer chose all three |
| Keep all three advisory (ADR-1595 as it stands) | No added runner time | A pull request that breaks the image or the zip merges, and the push run on master is the first gate | The reason ADR-1595 deferred it (unknown runtime) is now measured |
| Add the existing job names (`Build and test (amd64)`, ...) to `required` and keep the trigger paths | Smallest diff | `test_ci_impact.py` refuses trigger filters on required-context workflows; a workflow that fails to start reads as a pass; matrix names change with the matrix | Violates the ADR-1140 contract instead of following it |
| Route the dry run through `plan-ci-impact.py` too | One planner for all three | Full mode (every CI-authority change) would start five image builds and three GPU builds of up to 120 minutes; ADR-1595 chose the per-group plan for this reason | Its plan already starts on every pull request and works as a required context unchanged |
| Exempt the two tester selectors from the planner full mode | Keeps the former selection rate (see below) | A routing rule only these selectors follow; a change to the planner, the map or the workflow itself would no longer rebuild the image | A third routing mechanism next to trigger filters and the planner; failing closed is the point of ADR-1140 |
| Give the dry run a push trigger so it is strict on both events | No event-scoped list | Repeats every dry run on master after the pull request ran it; ADR-1595 has no push trigger on purpose | Costs a second run for no new evidence |

## Consequences

- **Positive**: a pull request whose tester image, Windows zip or release dry run
  fails, or whose workflow fails to start, cannot merge; the dry run reports on
  every pull request; the tester workflows follow the same planner, work and gate
  contract as the other consumer workflows, so the contract tests cover them.
- **Negative**: the planner runs everything when it cannot prove a narrower
  scope, and the two tester selectors inherit that. Over the 200 first-parent
  master commits `374e4342a..3d9f162bd` (2026-10-02 to 2026-10-05) the former
  path filters would have started the tester image on 41 and the Windows zip on
  10; the planner selects them on 148 and 142. All 142 Windows selections are
  full plans: `scripts/ci/**` changed in 112, a workflow that hosts a required
  context in 35, `.standards-baseline.json` in 29. At the medians above that is
  about 7000 instead of 1900 tester-image minutes and 5300 instead of 370
  Windows minutes per 200 pull requests. A master push whose plan selects them
  also makes the master aggregator wait for the push legs (both image
  architectures, four Windows zips), and a failed push leg turns it red.
- **Neutral / follow-ups**: the `tester_image` patterns keep the ADR-1595 list,
  which omits `core/`, `model/`, `python/` and `compat/` although
  `docker/Dockerfile.tester` copies them; a change there is still first built by
  the push run. Whether to narrow the full-mode cost (measured above) or widen
  the image inputs is a separate decision. `scripts/ci/tests/test_required_release_legs.py`
  holds the contract and plants each defect it refuses.

## References

- `Q1.1` (popup, 2026-10-05): "All three, path-aware (Recommended)".
- [ADR-1595](1595-pr-time-verify-push-only-workflows.md) (extended),
  [ADR-0313](0313-ci-required-checks-aggregator.md), [ADR-1140](1140-ci-impact-planner.md),
  [ADR-1151](1151-vmafx-first-release-1-0-0.md), [ADR-1297](1297-ci-gate-every-reporting-check.md).
