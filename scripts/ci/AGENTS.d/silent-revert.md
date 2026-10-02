---
paths:
  - scripts/ci/check-silent-revert.py
  - scripts/ci/silent-revert-allowlist.json
  - scripts/ci/tests/test_check_silent_revert.py
invariant: Measure the merge result, exclude merge commits from intent, use the live target tip, fail closed.
---
<!-- markdownlint-disable MD013 MD060 -->
# check-silent-revert.py invariants (ADR-1284 / ADR-1291)

Gate reports what merge removes from target that branch never set out to touch.
Four load-bearing properties. Drop one, gate becomes decoration.

1. **Measure merge result, not branch tree.** `merge_result_tree()` runs
   `git merge-tree --write-tree base head` — tree that squash merge commits.
   `git diff base..head` is no substitute: reports every file target changed
   and branch never touched. Red on any behind branch.
2. **Intent excludes merge commits.** `branch_intent()` unions diffs of
   `git rev-list --no-merges merge_base..head`. Conflict resolution is not
   branch work. Resolutions taken against target are what `dropped` and
   `resurrected` hunt. Re-adding `--merges` makes every bad resolution
   self-justifying. Both detectors then check surviving tree — `dropped` needs
   line absent from merged blob, `resurrected` absent from base blob. Drop that
   check and both fire on diff-alignment artefact: insert text above line,
   cumulative diff re-pairs line as delete plus add, no single commit diff shows
   pair.
3. **Workflow resolves live target tip.** `Silent-Revert Guard` fetches
   `github.event.pull_request.base.ref`, uses its tip. Never `base.sha` —
   `base.sha` records base branch at PR open, and defect class is target
   moving afterwards.
4. **Fail closed.** Unresolvable ref, no merge base, merge that does not
   resolve cleanly, git without `merge-tree --write-tree`: all exit non-zero.
   Never print `clean` for case gate could not analyse.

There are two declaration mechanisms. A one-off deliberate revert declares the
whole PR with a `revert:` title, `reverts: #N`, or
`intentional revert: <reason>`. An accepted ADR that requires restoring work
the target once lost uses `silent-revert-allowlist.json`, constrained by
detector, exact path, exact full commit for `reverse-hunk`, and a regex that
matches every evidence line. The latter is an expiring declaration, not a path
suppression: remove it when the finding disappears. Never add a bare path or
source-tree exclusion, and never loosen an entry to make a new finding match.
`GENERATED_PREFIXES`
covers rendered files only; never widen it to source trees.
`is_evidence()` drops conflict markers — `0c494cca0` committed three into
`core/src/feature/cuda/integer_vif_cuda.c`, and PR deleting them reset file to
pre-marker blob.

Regression: `python3 scripts/ci/tests/test_check_silent_revert.py` (22 tests).
