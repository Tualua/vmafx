---
paths:
  - scripts/ci/pr-body-input.sh
  - scripts/ci/deliverables-check.sh
  - scripts/ci/validate-pr-body.sh
  - scripts/ci/ffmpeg-patches-surface-check.sh
  - scripts/ci/state-md-touch-check.sh
  - scripts/ci/.shellcheckrc
  - scripts/ci/tests/test-pr-body-input-selection.sh
invariant: Four gates source `pr-body-input.sh` from their own directory: classify stdin, read, close; never test `[ ! -t 0 ]`.
---
<!-- markdownlint-disable MD013 MD060 -->
# PR-body stdin classification (`pr-body-input.sh`)

**Invariant — hard sibling-file dependency.** Four scripts source
`scripts/ci/pr-body-input.sh` unconditionally, before any input branch, each
resolving it from its own `${BASH_SOURCE[0]}` directory rather than from
`$PWD`: `deliverables-check.sh`, `validate-pr-body.sh`,
`ffmpeg-patches-surface-check.sh`, `state-md-touch-check.sh`. None of the four
is standalone any more. Consequences to preserve:

- Copying, vendoring or relocating one of those scripts **must** carry
  `pr-body-input.sh` with it, into the same directory. A copy that loses the
  sibling does not degrade — it dies at the `.` line before it reads anything.
- `pr-body-input.sh` is sourced, never executed, and defines only
  `pr_body_*` functions and the `PR_BODY_STDIN_KIND` / `PR_BODY_STDIN_FD`
  variables. Do not give it top-level side effects: it runs inside four gates
  that have already set `set -euo pipefail` and their own `trap … EXIT`.
- The rebase-sensitive surfaces table above lists these scripts against their
  workflow jobs. A workflow that invokes one of them by path is invoking two
  files; a checkout or artifact that ships only the named script is broken.
- `scripts/ci/.shellcheckrc` sets `external-sources=true` **because** of this
  dependency. The pre-commit `shellcheck` hook passes only the staged files, so
  staging one of the four without the helper made ShellCheck emit SC1091 ("not
  specified as input") on a file that is fine. `external-sources` makes it
  follow the `# shellcheck source=` directives the four already carry — more
  analysis, not less: measured over all 57 `scripts/ci/*.sh` checked one at a
  time, 4 findings before and 0 after, with nothing new introduced and no
  effect on the 120 shell scripts outside this directory. Keep the directive
  lines when editing these scripts, and do not reach for
  `# shellcheck disable=SC1091` instead.

**Invariant — never test `[ ! -t 0 ]` for "was a body piped".** That asks
whether fd 0 is a terminal, which is true for `/dev/null`, for a CI step's
null stdin, and for a *closed* descriptor. On a closed fd 0
`PR_BODY="$(cat)"` does not fail — it **deadlocks**, because the command
substitution's pipe takes the freed descriptor 0 and `cat` reads the pipe it
is writing to. All four gates hung this way; measured at `timeout 12` → 124.
New gates that read a PR body call `pr_body_classify_stdin` instead.

**Invariant — classify, read, close.** `pr_body_classify_stdin` hands the
caller a *duplicate* of fd 0 in `PR_BODY_STDIN_FD`; the duplicate is what
makes the subsequent read safe, since no later command substitution can claim
a descriptor already in use. The caller must release it with
`pr_body_close_stdin` after reading. That release cannot move inside
`pr_body_read_stdin`: callers invoke it as `"$(pr_body_read_stdin)"`, and an
`exec {fd}<&-` in a subshell closes the subshell's copy while the caller's
stays open — inherited by every `git`, `python3` and `mktemp` the gate spawns
afterwards.

**Invariant — an absent body means different things to different gates.**
`deliverables-check.sh` / `validate-pr-body.sh` exist only to parse a body, so
no body is exit 2. `ffmpeg-patches-surface-check.sh` /
`state-md-touch-check.sh` also have a diff, so no body makes their opt-out
sentinel unclaimable and they fall through to the diff check. Do not
"harmonise" the two families: turning the latter pair into exit 2 makes a
missing body abort the gate instead of enforcing it. They deliberately also
differ on `$PR_BODY` precedence — `+x` (set counts) for the first pair, `-n`
(non-empty) for the second, because only the first pair reports the two cases
differently.

`scripts/ci/tests/test-pr-body-input-selection.sh` pins all of the above for
all four gates, bounded by `timeout` so a re-regression reports a hang rather
than becoming one. It is wired as a pre-commit/pre-push hook whose `files:`
pattern lists every gate; adding a fifth caller means adding it there too.
