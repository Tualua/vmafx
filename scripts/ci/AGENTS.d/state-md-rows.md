---
paths:
  - scripts/ci/check-state-md-rows.sh
  - scripts/ci/tests/test-check-state-md-rows.sh
invariant: Four checks, none may narrow; a status token must agree with its section; a hit is fixed by moving the row.
---
<!-- markdownlint-disable MD013 MD060 -->
# check-state-md-rows.sh — the status token belongs to the section (ADR-0165)

Four independent checks, all of them widened only after a narrower version
reported a dirty file as clean. Do not narrow any of them.

1. Duplicate bug id. Matches four id shapes (`**T-ID**`, `T-ID`, `**T7-16**`,
   `Netflix/vmaf#NNN`) and anchors on the token that OPENS the first cell, not
   on the whole cell — most rows carry a description after the id.
2. Verbatim repeated row, for the ~143 prose-led rows that carry no id.
   Normalises away `_(verified YYYY-MM-DD: ...)_` before comparing.
3. Section against status. A row's status cell — the column the table header
   calls `Status`, else the last non-empty cell — must agree with the level-2
   heading the row sits under whenever the token OPENING that cell is a status
   word: `closed` / `fixed` / `resolved` / `done` only under
   `## Recently closed`, `open` only under `## Open bugs`.
4. Open row against move tombstone. A comment under `## Open bugs` that says an
   id "moved to Recently closed" is an explicit closed-state claim; the same id
   may not still have a table row in that section, even when the row has no
   parseable Status cell.

Check 3 exists because checks 1 and 2 only see a *duplicate*. A resolved row
left under `## Open bugs` with no second copy is invisible to both, and reads
as an open bug forever; 24 of 62 rows were in that state on 2026-09-21.

Check 4 covers the remaining no-Status shape. The 2026-09-08 PTQ bookkeeping
change claimed a row had moved and left its tombstone immediately below the
unchanged Open row; the first three checks all reported clean. Preserve the
fixture that rejects that exact contradiction. A tombstone plus the row under
`## Recently closed` is the valid moved state and must continue to pass.

Invariants for check 3:

- The judged cell is the `Status` column when a table header names one and the
  last non-empty cell otherwise, and the token read is the word that OPENS it.
  Requiring the whole cell to BE a status token caught `| fixed |` and skipped
  `| fixed (PR #1425) |` — the same misfiled row, one parenthetical later —
  along with 20 other live rows. Reading the leading word leaves the shapes
  that make no claim unjudged: a verification date or a parenthetical leads
  with no word at all, a branch name leads with `fix` rather than `fixed`
  (`fix/...`, `ci/...`), prose leads outside the vocabulary. Widening the
  vocabulary to guess at those fabricates failures — eight live rows end in a
  branch name. Cells are split on unescaped `|` only: `\|` inside an inline
  code span is the escaped pipe the renderer shows, not a boundary, and
  splitting on it shifts every later cell by one.
- Check 3 is a floor on this class of drift, not a proof of its absence. It
  reads one cell per row, so a status it does not recognise — buried mid-cell,
  in a column that is neither the last nor headed `Status`, or spelled outside
  the vocabulary — is passed over in silence and the file still reports clean.
- Of its two silent-disable paths, it fails closed on ONE. If a row claims a
  status whose owning section heading is absent, the gate errors rather than
  passing over rows that have become ungated, so renaming `## Open bugs` or
  `## Recently closed` breaks the build on purpose. The other path — a status
  cell the extraction does not recognise — is uncovered, and is the likelier
  of the two, since it needs one row edit rather than a heading rename. This
  file and ADR-0165 both asserted that the check fails closed on its *one*
  silent-disable path; that was false when written and the claim is corrected
  here rather than left standing.
- The fix for a hit is always to MOVE the row. Rewriting the status to match
  where the row landed is the failure, dressed up as the repair.

Header rows are excluded the same way for all three checks: a `|---|---|`
separator retracts the record for the line immediately above it, and only when
that line is itself a table row — `prev` and `prevline` both reset on a
non-table line. A separator whose header was lost to a dropped rebase hunk
otherwise retracts the status of the last data row above the blank line, which
silently unjudges a real row.
