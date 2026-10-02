<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1454: A large subtree `AGENTS.md` is a generated index over one page per topic

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `agents`, `docs`, `ci`, `rc3`, `fork-local`

## Context

Every directory with invariants an agent must respect carries an `AGENTS.md`.
An agent reads the files on the path from the repository root to the
directory it works in. On `origin/master` at `64faf8484` there are 71 such
files holding 1,330,232 bytes, and 16 of them are larger than 25,000 bytes.
An agent that changes one CUDA twin reads `core/AGENTS.md` (61,384 bytes),
`core/src/AGENTS.md` (34,402), `core/src/feature/AGENTS.md` (124,921) and
`core/src/feature/cuda/AGENTS.md` (80,647): 301,354 bytes, about 75,000
tokens, almost all of it about features it does not touch.

Two things make this worse over time. Every pull request that establishes an
invariant appends to one of these files, so they only grow. And because every
such pull request edits the same file, they conflict with each other; the
merge train resolves the same hunks by hand again and again.

A register pass over the files could not shrink them (pull requests
1755, 1756, 1758, 1760 and 1763): the text was already terse. The size is
the number of invariants, not the wording. The fix has to be about which
invariants an agent loads, not about how each one is written.

## Decision

A subtree `AGENTS.md` larger than 25,000 bytes becomes a short generated index
plus one page per topic in an `AGENTS.d/` directory next to it. The maintainer
chose this shape; this ADR fixes the details.

- **Layout.** `<dir>/AGENTS.d/_index.md` holds the title and the rules that
  apply to every file of the directory. Every other `<dir>/AGENTS.d/<slug>.md`
  is one topic: one tool, one feature or one contract, never a date or a pull
  request.
- **Front matter.** A page starts with a two-key block: `paths:`, a list of
  repository-relative globs the page governs, and `invariant:`, one line of at
  most 120 characters.
- **Index.** `<dir>/AGENTS.md` is rendered from those files by
  `scripts/docs/agents_index.py` and never edited by hand. It holds, in this
  order: the title; an instruction to match the paths about to be touched
  against the table and read each matching page first; the rules from
  `_index.md`; and one table row per page with the governed paths, a link to
  the page and its one-line invariant. Rows are sorted by slug.
- **Wiring.** `make docs-fragments-write` renders every index and
  `make docs-fragments-check` fails on a stale one, the same way the ADR index
  and the exact-twin table are handled
  ([ADR-0221](0221-changelog-adr-fragment-pattern.md),
  [ADR-1428](1428-exact-twins-fragments.md)). The `check-generated-docs`
  pre-commit hook runs the check when an `AGENTS.md` or a file under an
  `AGENTS.d/` changes.
- **Hand edits fail.** The check also fails when the index was edited or
  appended to by hand, which is how the single files grew. The message quotes
  the lines the sources do not produce and names the `AGENTS.d/` directory
  they belong in.
- **Budgets.** The generator refuses an index above 16,000 bytes, a page
  above 12,000 bytes, an `invariant:` above 120 characters and a page with
  more than 24 globs. It also refuses a glob that matches no file, so a page
  cannot keep pointing at a file that was renamed or deleted.
- **Migration is a move.** A migration pull request moves text and changes
  nothing else. `scripts/docs/agents_migration_check.py` compares the old file
  with the new pages and fails unless every paragraph, top-level list item,
  table row and code block of the old file is present exactly as often as
  before, byte for byte; every heading text survives; and every load-bearing
  token (code span, path, ADR, pull-request, issue or ledger id, flag,
  identifier, number, link target) is still there. Only relative link targets
  change, because a page sits one directory deeper; the checker compares both
  sides after resolving them to the repository root.

### Why these budgets

The pilot, `scripts/ci/AGENTS.md`, went from 93,543 bytes to an index of
14,133 bytes and 46 pages of 806 to 6,373 bytes (median 1,818). A table row
costs 267 bytes on average. Three tasks, measured by matching their paths
against the table:

| Task | Pages read | Index + pages | Share of 93,543 bytes |
| --- | --- | --- | --- |
| Change the parity gate tolerance table (`gpu_ulp_calibration.yaml`) | `parity-gate` | 19,186 bytes | 21% |
| Add a tidy lane (`tidy-ratchet.py`, a `tidy-baseline-*.json`) | `tidy-ratchet` | 20,506 bytes | 22% |
| Touch the deliverables check (`deliverables-check.sh`) | `pr-body-stdin`, `pr-body-validator` | 21,592 bytes | 23% |

The index is the part every agent pays, so it has the tighter budget relative
to what it replaces: 16,000 bytes is about 4,000 tokens, and leaves the pilot
room for seven more rows. With a row at about 170 bytes besides its path list,
the bytes an agent loads (index plus one or two pages) are lowest near
`sqrt(1.5 * file_bytes / 170)` pages, about 29 for the pilot, and the curve is
flat from 25 to 50 pages, so the finer cut costs about 5% and buys smaller
units of change. The page budget of 12,000 bytes is about 3,000 tokens and
twice the largest pilot page; a page that reaches it holds more than one
topic.

### What was checked before choosing `AGENTS.d/`

- Nothing in the repository globs `**/AGENTS.md` in a way the new directory
  breaks. `core/doc/Doxyfile.public-api` excludes `*/AGENTS.md` but only reads
  `*.h`. `tools/markdownlint/verify.mjs`, `.github/CODEOWNERS`,
  `.github/ci-impact.json` and `scripts/ci/check-local-data-contract.sh` name
  the root file only. `scripts/dev/project_modernization_audit.py` skipped
  files named `AGENTS.md`; it now also skips `AGENTS.d/`.
- REUSE: the default annotation in `REUSE.toml` covers `*/**`, so the pages
  are licensed like the file they came from.
- Markdown lint: nested `AGENTS.md` files and the pages are style-checked by
  the same configuration, which accepts front matter. A page carries the same
  `markdownlint-disable` comment as the file it came from.
- MkDocs builds `docs/` only; the pages are outside it.
- The CI impact planner classifies by top-level prefix, so a page is routed
  like the `AGENTS.md` next to it.
- Pre-commit hooks whose `files:` pattern named `scripts/ci/AGENTS.md` because
  it documents their contract now also name the page that holds that text.
- Two files outside this change depend on the text of a subtree `AGENTS.md`
  that is above the threshold: `ai/tests/test_legacy_extractor_manifests.py`
  reads `core/AGENTS.md`, and `scripts/ci/check-issue-reference-provenance.py`
  holds contracts keyed on `core/src/feature/cuda/AGENTS.md`. Neither concerns
  the pilot; the pull request that migrates such a file moves the reference to
  the page that receives the text.

## Alternatives considered

| Option | Pros | Cons | Verdict |
| --- | --- | --- | --- |
| Leave the files as they are | No work | 75,000 tokens for one CUDA twin; grows with every pull request; permanent merge hot spot | Rejected |
| Register pass only (shorter wording) | No structural change | Tried in five pull requests; the text was already terse, so the files did not shrink | Rejected: measured, no effect |
| One page per file path | An agent finds the page from the path without a table | Most invariants span several files (a gate, its test, its workflow step), so each would be repeated or arbitrarily assigned; hundreds of pages | Rejected |
| Pages next to the index (`AGENTS.<topic>.md`) | Relative links stay valid | 46 more files in `scripts/ci/`, more in `core/src/feature/`; clutters the directory the agent lists | Rejected: a subdirectory keeps the source tree readable, and the link change is mechanical and checked |
| Hand-written index | No generator | The index is one shared file again: every pull request that adds a page edits it and conflicts; rows drift from the pages | Rejected: it recreates the hot spot |
| Generated block inside a hand-written index (sentinel comments) | Rules for the whole directory stay in the index file | Two owners for one file; a hand edit outside the block and a regeneration inside it still meet in one file | Rejected: `_index.md` gives the hand-written part its own file |
| Index generated from page front matter | A pull request adds or edits one page; the index is regenerated, and a conflict in it is resolved by regenerating | A generator and a checker to maintain | **Chosen** |

## Consequences

- **Positive**: an agent reads the index and the pages for the paths it
  touches: 21% to 23% of the old file in the pilot.
- **Positive**: a pull request that records an invariant adds or edits one
  page. Two such pull requests touch different files. Editing a page body does
  not change the index at all; only a new page or a front-matter change does.
- **Positive**: a glob that no longer matches a file fails the gate, so a page
  cannot silently outlive the code it describes.
- **Negative**: the pages together are larger than the old file (109,066
  against 93,543 bytes in the pilot) because each carries front matter, a
  title and, where table rows were split, a repeated table header.
- **Negative**: sentences that point "above" or "below" inside the old file
  now point across pages. A migration does not reword them; the index is how
  the other page is found.
- **Negative**: the budgets are hard limits. A page or an index that reaches
  its budget has to be split or tightened in the pull request that hits it.
- **Neutral / follow-ups**: 15 files above 25,000 bytes remain. They are
  migrated one per pull request following the pilot;
  `T-AGENTS-INDEX-MIGRATION-2026-10-02` in [`docs/state.md`](../state.md)
  lists them. Files below the threshold stay as they are.

## References

- `Q` (maintainer popup answer, 2026-10-02): "Index + one page per topic
  (Recommended)".
- `req` (maintainer brief, 2026-10-02, paraphrased): content is moved, not
  rewritten; a checker proves that no fact is lost; the pilot is
  `scripts/ci/AGENTS.md`.
- [ADR-0221](0221-changelog-adr-fragment-pattern.md) (fragment pattern),
  [ADR-1428](1428-exact-twins-fragments.md) (its most recent use),
  [ADR-0108](0108-deep-dive-deliverables-rule.md) (the `AGENTS.md` invariant
  note as a deliverable).
- Contributor guide: [agents index](../development/agents-index.md).
