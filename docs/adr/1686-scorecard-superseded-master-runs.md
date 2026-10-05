<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1686: a Scorecard master run whose master moved on to a descendant ends cancelled

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: lusoris
- **Tags**: `ci`, `security`, `scorecard`, `github-actions`, `merge-train`, `rc3`, `fork-local`
- **Refines**: [ADR-1247](1247-scorecard-exact-head-gates.md)

## Context

The `Scorecard Master Gate` of [ADR-1247](1247-scorecard-exact-head-gates.md)
attributes the publisher's report to `github.sha` only while the live master ref
still names that commit: the pinned action reads master through separate GraphQL
and archive calls, so a report scanned while master moved may mix two commits.
ADR-1247 records that such a run fails. The merge train lands a commit every 10
to 20 minutes, and a scan with its queue time takes about 25 minutes, so a master
commit followed by a landing during its scan ends **failure** although nothing
is wrong. Run 37276297218 (master `b0d991df7`, report score 8.7) read master at
`53e582831`, one commit ahead, and failed with `remote master moved or its final
ref is invalid`. ADR-1673 (pull request #2066) stops concurrency from cancelling
master push runs, so every superseded commit then finishes its scan and fails
this way.

A superseded run has no verdict. It must not be green (HISS-18: a gate that did
not run is never reported as passing) and should not be red. A GitHub Actions
job has four conclusions: `success` and `failure` from the exit status (GitHub's
exit-code reference offers nothing else), `skipped` from an `if` that is false
before the job starts, and `cancelled` from a cancellation of its run.

## Decision

The gate splits the refusal that `master_identity()` used to raise for every
case. A final ref that is not `refs/heads/master`, not a commit or not a full
lowercase SHA is a **failure**. A final ref equal to `github.sha` gives the
normal **verdict**. A final ref naming another commit is compared through
`repos/{repo}/compare/{sha}...{newer}`; status `ahead` with `behind_by` 0, base
and merge base equal to `github.sha` and the newest compared commit equal to
the ref makes the run **superseded**. Any other answer (behind, diverged,
identical, malformed, unreadable) is a **failure**, as is a move whose ancestry
was not read.

A superseded run writes a receipt (`outcome: superseded`, the newer commit and
its distance), a step summary and a notice naming the newer commit, and
`scorecard_gate.py` exits 3. The policy step then cancels its own run
(`POST /repos/{repo}/actions/runs/{run_id}/cancel`) and waits at most 120 s to
be stopped; a cancel that never lands ends the step red. For this the gate job
holds `actions: write`, and its `if` is `!cancelled()` instead of `always()`,
because GitHub keeps running a job whose condition is still true when its run is
cancelled. The newer commit's own push run gives the verdict: the push trigger
starts one, and `concurrency: scorecard-${{ github.ref }}` with
`cancel-in-progress: false` keeps the newest pending run.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Superseded run cancels itself (job-level `actions: write`) | No verdict reads as no verdict: neither green nor red; the merge train's sentinel log already reads `cancelled` as "no verdict" | The gate job's token can cancel and re-run this repository's workflow runs; a live cancel must arrive, otherwise the run is red | Chosen |
| Fail as today | No new permission | Master is red after every landing during a scan; real Scorecard failures hide among false ones | Master red with nothing wrong is the defect |
| Skip the gate job on a superseded run (an earlier job decides, the gate's `if` is false) | No write permission | A skipped job leaves the run green: a gate that did not run reads as passing (HISS-18) | Violates HISS-18 |
| Exit 0 with a notice, or `continue-on-error` | Simplest | The run is green without a verdict | Violates HISS-18 |
| Serialise landings around scans (the train waits for the previous master's scan) | Every commit gets a verdict | Ties merge-train throughput to a 25-minute hosted scan on a saturated queue, one landing per scan | Cost far above the value of a verdict per commit |
| Attribute the report to the newer commit | A verdict every time | The report may mix one commit's GraphQL data with another's archive bytes; ADR-1247 forbids relabelling | Unsound evidence |

## Consequences

- **Positive**: a landing during a scan no longer turns master red. The
  receipt, summary and notice name the commit whose run carries the verdict, and
  the artifact keeps the final ref and the comparison read
  (`master-compare.json`) for replay.
- **Negative**: the gate job's token has `actions: write`. The job runs only
  master code at `github.sha` and SHA-pinned actions on push, schedule and
  branch-protection events, never pull-request code. Scorecard 5.5's
  Token-Permissions check warns on a job-level `actions: write` and keeps the
  score (its `docs/checks.md`). No other repository gate reads job permissions:
  zizmor is not used here, `scripts/dev/check_repository_security.py` reads
  rulesets, and praetor compares the repository's default workflow
  permissions, not jobs. `scripts/ci/tests/test_scorecard_workflow.py` pins the
  permission, its reason and the mechanism.
- **Negative**: while master moves faster than a scan, consecutive runs can all
  be superseded and no commit gets a Scorecard verdict until master stays still
  for one scan (or the weekly scheduled run falls into a quiet window). Before
  this decision those runs failed and gave no verdict either.
- **Neutral / follow-ups**: the Required Checks Aggregator accepts only
  `success` for `Scorecard Master Gate`, so on a master push whose aggregator
  runs to completion (the merge train's sentinel commit) a superseded gate still
  fails the aggregate, as the failure did before. How the aggregator treats a
  superseded gate on a push is a separate decision. A superseding commit whose
  push starts no workflow (`[skip ci]` in its message) has no run of its own;
  the next push that starts one gives the verdict. The live conclusion is
  observed only when a superseded master run happens; the design rests on
  GitHub's cancellation reference and the runner source (a job cancellation
  sets the job result to `Canceled`, and file commands including
  `GITHUB_STEP_SUMMARY` are processed in a `finally` block).

## References

- [ADR-1247](1247-scorecard-exact-head-gates.md) (refined: its "that run
  fails" consequence now applies to a move that is not to a descendant);
  ADR-1673, pull request #2066 (master push runs not cancelled by concurrency).
- GitHub: [workflow cancellation reference](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-cancellation)
  (a job whose `if` evaluates true is not cancelled),
  [`always()` and `cancelled()`](https://docs.github.com/en/actions/reference/workflows-and-actions/expressions#always),
  [exit codes](https://docs.github.com/en/actions/how-tos/create-and-publish-actions/set-exit-codes),
  [cancel a workflow run](https://docs.github.com/en/rest/actions/workflow-runs#cancel-a-workflow-run)
  (Actions: write),
  [compare two commits](https://docs.github.com/en/rest/commits/commits#compare-two-commits).
- actions/runner: `src/Runner.Worker/StepsRunner.cs` (job cancellation sets
  `TaskResult.Canceled`), `src/Runner.Worker/ActionRunner.cs`
  (`fileCommandManager.ProcessFiles` in `finally`),
  `src/Runner.Common/Util/TaskResultUtil.cs`.
- [Scorecard 5.5 Token-Permissions](https://github.com/ossf/scorecard/blob/c395761df6afe1a69e476bc60a013a94bcbc153f/docs/checks.md#token-permissions).
- Run [37276297218](https://github.com/VMAFx/vmafx/actions/runs/37276297218):
  final ref `53e58283139c2eb8b6290ae12a33d7d12da4c51c`, comparison `ahead_by: 1`.
- Source: RC3 master-green task of 2026-10-05 (merge-train handoff brief
  `scorecard-superseded`); no direct user quote.
