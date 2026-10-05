<!-- markdownlint-disable MD013 MD060 -->
# ADR-1701: Build and test the tester image every night on master

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: lusoris
- **Tags**: `ci`, `release`, `docker`, `testing`

## Context

The pull-request input list of the tester image (selector `tester_image`,
[ADR-1687](1687-required-release-dry-run-legs.md),
[ADR-1700](1700-tester-selectors-own-paths-only.md)) is the ADR-1595 path list:
the Dockerfile, `tools/rc1-tester/`, the toolkit install scripts, the licence
inputs. `docker/Dockerfile.tester` also copies `core/`, `model/`, `python/` and
`compat/`, so a library change can break the image without starting its build;
until now the first build of such a change was the next push that touched a
listed input, or a publish dispatch. Widening the selector to the library would
start a 47-minute build on most library pull requests. The maintainer decided
(2026-10-05) to keep the pull-request list narrow and add a nightly build of
master.

## Decision

We will add a `schedule` trigger to `docker-publish-tester.yml`, every night at
00:29 UTC:

- It builds master's head (scheduled runs use the latest commit of the default
  branch), the amd64 image only, runs the documented `docker run` line and
  validates the report. `validate` narrows the matrix to amd64 on `schedule` as
  on `pull_request`; `build-gpu` excludes the event; every push, sign and attest
  step stays behind `publish == 'true'`, which a scheduled run never sets.
- At the job timeouts (impact 10, validate 10, reference scores 45, build 90,
  gate 5 minutes) it ends by 03:09, before `nightly.yml` (03:17) and the weekly
  Release Dry Run (Wednesday 03:41) start; GitHub may start a scheduled run late
  under load. It has a concurrency group of its own (`nightly`), so it never
  waits on, cancels or holds up a push run or a publish dispatch.
- A red nightly is a failed `schedule` run of `Publish Tester Image` on master.
  GitHub sends the notification to the user who last changed the cron line, and
  the forge check of `praetorctl audit` reports the workflow as failing on master
  (it reads the latest non-pull-request runs on the default branch).

`scripts/ci/tests/test_pr_time_verify_workflows.py` runs `validate` for the
schedule event (amd64, `publish=false`), checks that no job a scheduled run
reaches pushes, signs or attests outside `publish == 'true'`, and checks the
timeouts against both other night schedules.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Widen `tester_image` to `core/`, `model/`, `python/`, `compat/` | A pull request that breaks the image through shared code is caught before merge | A 47-minute build on most library pull requests | One build a night finds it within a day |
| Build both architectures nightly | Covers the arm64 image too | A second native runner every night; the arm64-specific code paths are built by the required `Ubuntu ARM clang` and `Windows ARM64 MSVC` lanes, and the push run builds the arm64 image when tester inputs change | amd64 is enough to catch a library change that breaks the image build or its report |
| Weekly instead of nightly | Cheaper | Up to a week of merges before a break is found | The merge train lands several batches a day |
| Include the GPU images | Covers the three kits | Up to six hours of runner time a night | Out of proportion; the push run and the stage contract tests cover them |

## Consequences

- **Positive**: a library change that breaks the tester image is found within a
  day of its merge, without slowing pull requests.
- **Negative**: one amd64 build a night (median 47 minutes); a break is found
  after it merged, not before.
- **Neutral / follow-ups**: the slot must stay ahead of the other night jobs; the
  test fails when a timeout or a cron line moves it into them. In a public
  repository GitHub disables scheduled workflows after 60 days without activity.

## References

- `Q2.2` (popup, 2026-10-05): "Narrow + nightly master build (Recommended)".
- GitHub Docs, "Events that trigger workflows", `schedule`: scheduled workflows run
  on the latest commit of the default branch; notifications go to the user who
  last modified the cron syntax; runs can be delayed under load; public
  repositories disable them after 60 days without activity.
- [ADR-1687](1687-required-release-dry-run-legs.md), [ADR-1700](1700-tester-selectors-own-paths-only.md),
  [ADR-1595](1595-pr-time-verify-push-only-workflows.md).
