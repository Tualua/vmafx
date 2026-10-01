<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1428: Exact GPU twins are declared by one fragment file each, not by a shared literal

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `gpu-parity`, `ci`, `testing`, `docs`, `rc3`, `fork-local`

## Context

[ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) made a parity-gate cell an
equality when both sides are the CPU extractor or a twin listed in
`EXACT_TWINS` in `scripts/ci/cross_backend_calibration.py`. Every RC3 pull
request that makes a twin bit-identical lists it there. Each one therefore
edited the same four places: the `EXACT_TWINS` dict literal, the test that
asserted the whole dict, the table row and "Exact twins" paragraph in
`docs/development/cross-backend-gate.md`, and the "Exact twins" paragraph in
`scripts/ci/AGENTS.md`. Ten open pull requests conflicted pairwise on these
lines, and each conflict was resolved by hand before the train could land it.

The repository already removed the same class of conflict for the changelog
and the ADR index ([ADR-0221](0221-changelog-adr-fragment-pattern.md)): one
file per entry, the shared list rendered from the files.

## Decision

We will declare an exact twin by adding one file,
`scripts/ci/exact_twins.d/<feature>.<backend>`, and editing nothing shared.

- A fragment holds `key: value` lines: `adr:` (one or more `ADR-NNNN`, each an
  existing file under `docs/adr/`) and `evidence:` (one line: fixtures and
  result). Any other key, a missing key, a malformed value, an empty file or a
  name that is not `<feature>.<backend>` fails the loader.
- `cross_backend_calibration.py` builds `EXACT_TWINS` from the directory at
  import, sorted. A missing or empty directory raises instead of meaning "no
  exact twins". The public names and behaviour of `EXACT_TWINS`,
  `is_exact_pair` and the `EXACT_TWIN_*` constants do not change.
- `cross_backend_parity_gate.py` checks every fragment against its
  `FEATURE_METRICS` and `BACKEND_SUFFIX` at import, so a fragment naming an
  unknown feature or backend fails the gate instead of never matching a cell.
- `docs/development/cross-backend-exact-twins.md` is generated from the
  fragments by `scripts/docs/generate-exact-twins.py`, wired into
  `make docs-fragments-write` and `make docs-fragments-check`. On a merge
  conflict the generated file takes master's side and is regenerated.
- The tests assert properties that hold for any set of fragments, not the
  set itself.

The rule for listing is unchanged: a measurement that shows bit-identity and
an ADR that records it. A listed twin that drifts is fixed, never given a
tolerance.

## Alternatives considered

| Option | Pros | Cons | Verdict |
| --- | --- | --- | --- |
| Keep the dict literal | No new mechanism | Conflicts on every RC3 pull request, in four files | Rejected: the cost is paid on every landing |
| One data file (YAML or JSON) listing all twins | One parser, one place to read | Still one shared file; two additions in neighbouring lines conflict | Rejected: moves the hotspot, does not remove it |
| One file per feature | Fewer files | Two backends of one feature landing together edit the same file and conflict | Rejected |
| One file per (feature, backend) | Two pull requests touch disjoint files; the name is the key, so a duplicate cannot exist | One more directory and a generator | Chosen |

## Consequences

- **Positive**: declaring a twin exact is one added file; no shared line is
  edited, so the pull requests no longer conflict with each other.
- **Positive**: the documentation table cannot drift from the gate, because
  both read the same files.
- **Negative**: the pull requests open when this lands conflict with it once.
  Each owner deletes their `EXACT_TWINS` hunk, the test assertions and the
  prose they added, and adds a fragment instead
  (`scripts/ci/AGENTS.md` lists the steps).
- **Neutral**: the parse is a hand-written `key: value` reader, so no new
  dependency enters the gate ([HISS-11](../../AGENTS.md)).

## References

- `req` (maintainer brief, 2026-10-01, paraphrased): remove the merge hotspot
  that every exact-twin pull request edits, using the fragment pattern the
  repository already applies to the changelog and the ADR index.
- [ADR-0221](0221-changelog-adr-fragment-pattern.md),
  [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
