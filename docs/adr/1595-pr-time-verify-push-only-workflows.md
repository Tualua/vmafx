<!-- markdownlint-disable MD013 MD060 -->
# ADR-1595: Build and run what the push-only and release-only workflows publish, before they publish

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: lusoris
- **Tags**: `ci`, `release`, `docker`, `supply-chain`, `testing`

## Context

Six workflows build artifacts and none of them ran on a pull request:
`docker-publish-tester.yml` and `windows-tester-bundle.yml` ran on a push to
master, `macos-tester-bundle.yml` only on dispatch, and
`docker-publish-production.yml`, `docker-publish-operator-node.yml` and
`supply-chain.yml` only when a release is published. A change to a Dockerfile, a
toolkit pin, the licence inputs or the `vmaf-mcp` package therefore reached the
release unbuilt, and the local merge train (which restacks and builds CPU and CUDA
only) was the only gate before master. A push-triggered run that fails on master
is a red master; a release-triggered run that fails is a failed release.

The six differ in cost. The tester image is two architectures plus three GPU
toolkit images (up to six hours of runner time). The Windows zips are three
builds of up to 150 minutes on Windows runners. The macOS bundle runs on a macOS
runner, which bills at ten times a Linux runner. The production and operator /
node images are five image builds, three of them GPU toolkits. The supply-chain
run is minutes, but half of its jobs need an OIDC token or a registry.

## Decision

We will verify each workflow before it publishes, sized to what its runner costs,
and never publish, sign or attest from a pull-request or scheduled run:

- **Tester image and Windows zip**: add a `pull_request` trigger with the path
  list of the existing `push` trigger. The `validate` job resolves the pull
  request's merge commit as the source with `publish=false` and narrows the build
  matrix: the amd64 image, and the x64 zip. The arm64 and GPU images and the
  arm64 and CUDA zips stay with the push run.
- **macOS bundle**: add a weekly `schedule` that builds and verifies master's
  head, with no publish; no per-pull-request run.
- **Production, operator / server / node and supply-chain**: add one workflow,
  `release-dry-run.yml`, that runs on every pull request but routes in-job
  (`scripts/ci/release-dry-run-plan.sh`) so a pull request builds only the group
  whose inputs it touches, and runs every group weekly. It builds the same
  Dockerfile targets for linux/amd64 without pushing and runs them through
  `scripts/ci/release-image-smoke.sh`; the GPU toolkit images are built, not
  loaded; it builds the `vmaf-mcp` wheel and sdist, generates its SBOMs and
  checks them with `scripts/release/verify-mcp-sbom.sh`, which `supply-chain.yml`
  now runs too, so the check that gates a release is the one a pull request
  exercised.
- `scripts/ci/tests/test_pr_time_verify_workflows.py` holds the result: it runs the
  `validate` step of the tester workflows for pull-request, push and schedule
  events, fails if `release-dry-run.yml` gains a credential, a push or an
  unpinned action, and fails if what the dry run builds differs from what the
  release builds.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Run each workflow's full jobs on every pull request | One implementation, no routing | Hours of Windows, arm64 and GPU runner time per pull request on a queue that is already saturated | Cost out of proportion to what the extra legs find |
| Weekly schedule only, for all six | Cheapest | A change reaches master and a week of other changes before it is found; no signal on the pull request that caused it | Slow feedback for the workflows whose inputs change often (Dockerfiles, `build-config.env`) |
| Make the release workflows reusable (`workflow_call`) and call them with a dry-run flag | One implementation of every step | Rewrites six release workflows that cannot be run here; a mistake costs a release; the publish steps are interleaved with the build steps | The release path is changed as little as possible: one verifier extracted and shared, the rest mirrored and held in lockstep by a test |
| Path-filtered `pull_request` trigger on `release-dry-run.yml` | Native, no plan job | Different groups need different paths; ADR-1140 forbids trigger filters on required-context workflows and ADR-1297 moves checks toward required | The in-job plan gives per-group routing and works unchanged if the checks become required |
| Make the new checks required in the aggregator | Blocks a merge on them | Absent-means-pass already covers the routed-out case, but the tester and Windows legs cost hours and the queue is saturated | Left for a decision once the runtime of the amd64 legs is measured on master |

## Consequences

- **Positive**: a Dockerfile, toolkit-pin, licence-input or `vmaf-mcp` change is
  built on its pull request; a bundle that stops building is found within a
  week; the SBOM check that gates a release has fixtures that refuse each planted
  defect.
- **Negative**: pull requests that change `build-config.env` or the licence inputs
  now start five image builds and three GPU builds (cache-read only, no write);
  the arm64 and GPU tester images and the arm64 and CUDA Windows zips are still
  first built by the merge; the HTTP startup smoke of the server images stays
  release-only.
- **Neutral / follow-ups**: measure the amd64 legs on master and decide whether to
  require them in `required-aggregator.yml`; a new image target or `vmaf-mcp`
  build command goes into the dry run and the release workflow together.

## References

- `req`: paraphrased: the push-only and release-only workflows the merge train
  cancels on master need a PR-time or local gate, sized to runner cost; per
  workflow, state what was chosen and why (lane brief B1, 2026-10-04).
- [ADR-1140](1140-ci-impact-planner.md), [ADR-1297](1297-ci-gate-every-reporting-check.md),
  [ADR-0819](0819-dev-container-ci-gate.md), [ADR-1347](1347-image-recovery-from-default-branch.md).
