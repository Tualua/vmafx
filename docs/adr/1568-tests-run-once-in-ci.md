<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1568: A test runs once in CI

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: Lusoris
- **Tags**: ci, testing, fork-local

## Context

[ADR-1528](1528-test-suite-registry.md) added the required `Tooling Tests` job. It runs every Python and shell test under `scripts/`, `dev/scripts/`, `testdata/` and two tool test directories. Before that job existed, about half of those files already ran somewhere else: in 61 workflow steps across eleven workflows (`Release Script Contract` was almost nothing else), and in 50 local pre-commit hooks that the CI `Pre-Commit` job runs with `--all-files`. Each of those tests therefore ran two or three times per pull request. The slowest duplicates measured on hosted runners were the duplicate-implementation gate test (111 s), the hook-install test (66 s) and the release-script tests (20 s), plus the share of the 10-minute `Pre-Commit` job those 50 hooks take (105 s measured locally). The maintainer directed that each test run once, with the registry as the single source.

Some tests cannot run in Tooling Tests as it stands: three of them render the chart with the pinned, checksum-verified helm that only the helm job installs, and `check_input_contract.py` requires the FFmpeg Patch Stack job to run `make ffmpeg-input-contract`.

## Decision

A test runs once in CI, in the job that owns its suite:

- Every workflow step that ran a test of the tooling suite is removed. A step that also ran a check of the live tree keeps the check.
- `scripts/ci/suite_registry.py check` fails when a workflow runs a tooling test again, so a removed duplicate cannot come back.
- The CI `Pre-Commit` job skips the local hooks whose entry runs only tooling tests. `suite_registry.py precommit-skip` names them, so the list cannot drift. The hooks stay active at commit time.
- A test that needs a tool only one job provides gets its own suite naming that job. The most specific registry path owns a file, so `helm-chart` takes three files out of `scripts/` and `ffmpeg-patches` takes `ffmpeg-patches/test/`. The helm impact selector covers the three helm tests.
- Tooling Tests installs what its other tests need: cppcheck from the distribution, as the Cppcheck job does, and mypy in its lock.

Contract tests that asserted their own workflow step now assert membership of the tooling suite (`suite_registry.suite_members`) instead.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Remove duplicates, guard with the registry check, skip test-only hooks in CI (chosen) | Each test runs once; the guard and the skip list derive from the registry; local commit-time feedback stays | Some gates no longer prove their own tests in the same job (Release Script Contract, the Scorecard jobs) | — |
| Keep the duplicates | No change | Two or three runs of each test; several minutes per pull request; the maintainer asked for one run | Wastes runner time without adding coverage |
| Delete the test-only pre-commit hooks | Simplest CI | Contributors lose the commit-time run, which is the fastest feedback | The duplication is in CI, not on the workstation |
| Move every tool into Tooling Tests (install helm there too) | One job for all script tests | A third copy of the pinned helm installer; the FFmpeg input contract would need its wiring rule rewritten | A suite per tool-owning job reuses what exists |

## Consequences

- **Positive**: per pull request, the removed workflow steps took about 3.9 minutes of hosted runner time (sum of their measured durations), and the skipped hooks about 1.75 minutes of the `Pre-Commit` job (local measurement). No test lost its run: `suite_registry.py check` proves every test file is in a suite that a required check runs, and it now refuses a second run.
- **Negative**: a gate whose job used to run its own tests first (Release Script Contract, the Scorecard PR and master gates, the daily FFmpeg refresh) now relies on Tooling Tests having passed on the same commit. The aggregator requires both, so a merge still needs both.
- **Neutral / follow-ups**: `build.yml` still runs `compat/vmaf/tests/test_decorator_extended.py` on macOS and Windows. These are platform runs of the `compat` suite (native POSIX and Windows lock implementations, HISS-21), not duplicates of a Linux run.

## References

- `req`: maintainer, 2026-10-04 (via the CI lane coordinator): "Duplicate runs: remove the older duplicate steps so each test runs once, with the registry as the single source; show the per-PR time saved and that no test lost its run (the registry check proves it)."
- [ADR-1528](1528-test-suite-registry.md), [ADR-1240](1240-ffmpeg-release-patch-lifecycle.md), [ADR-1140](1140-ci-impact-planner.md).
