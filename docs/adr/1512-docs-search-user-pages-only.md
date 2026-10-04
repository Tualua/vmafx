<!-- markdownlint-disable MD013 MD060 -->
# ADR-1512: Site search covers user pages only; record bodies leave the index

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: Lusoris
- **Tags**: docs, mkdocs, search, fork-local

## Context

Material for MkDocs loads the whole search index on the first page view. On
the site after [ADR-1510](1510-adr-nav-collapse-behind-index.md),
`search/search_index.json` was 29.3 MB raw and 8.9 MB gzipped, with 24,265
entries. Most of it was records: ADR bodies 34.5 % of the bytes, research
digests 23.4 %, `rebase-notes.md` 13.3 %, `state.md` 8.6 %, the changelog
archive 6.3 %. User pages made up about 14 %. Every reader paid for the
records on the first page view, and record text crowded user pages out of
the results.

## Decision

We will index user pages only. The bodies of ADRs (`docs/adr/NNNN-*.md`),
research digests, `docs/rebase-notes.md`, `docs/state.md` and the changelog
archive leave the search index. Record titles stay findable: the ADR index,
the ADR tag pages, the research index and two generated title lists, with
one heading per record, stay indexed.

- **Mechanism.** The page front matter `search: exclude: true`, which
  Material's search plugin reads (`SearchIndex.add_entry_from_context` in
  9.7.7) and Zensical honours. `.meta.yml` files apply it per directory
  through Material's built-in `meta` plugin (`material/meta`), which Zensical
  implements natively since 0.0.58. A new ADR or digest is excluded without
  any edit. Index pages override it with `search: exclude: false`. The two
  single files carry the front matter themselves.
- **Title lists.** `scripts/docs/generate-record-titles.py` writes
  `docs/adr/titles.md` and `docs/research/titles.md`. A long index page ranks
  low for a title query; a heading per record is a search entry of its own.
- **Checks.** `scripts/docs/check_search_scope.py` reads the built index and
  fails on a record page in it or an index page missing from it.
  `scripts/docs/tests/test_search_scope.py` builds a fixture with a planted
  ADR, digest and user page, once without the `meta` plugin (the ADR is
  indexed) and once with it (the ADR is not, the user page is).
- **Full text.** Record bodies are searched with GitHub code search
  (`repo:VMAFx/vmafx path:docs/adr/ <terms>`); the indexes and
  `docs/development/docs-site-design.md` say so.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| User pages only, titles through indexes and title lists (chosen) | index 82 % smaller; user pages lead the results; titles still found | record text needs GitHub code search | — |
| Keep everything indexed | full-text search of records on the site | 8.9 MB gzipped on every first page view; records crowd out user pages | the cost every reader pays |
| Exclude records with a MkDocs hook by path | no file in `docs/` changes | Zensical runs no hooks, so its search would index everything again | does not carry over to Zensical (ADR-1508) |
| Front matter in every record file | explicit per file | edits about 2,000 files, including Accepted ADR bodies that are frozen | churn and frozen bodies |
| Exclude only ADRs | smallest change | research, rebase notes and state are another 45 % | half the cost remains |

## Consequences

- **Positive**: the search index drops from 29.3 MB (8.9 MB gzipped) to
  5.8 MB (1.7 MB gzipped); user pages lead the results; a search for an ADR
  title still lands on its entry in the title list.
- **Negative**: a phrase that only occurs inside an ADR or digest body is no
  longer found by the site search.
- **Neutral / follow-ups**: a new record directory that should stay out of
  the index gets its own `.meta.yml`; `check_search_scope.py` names the
  record locations it expects.

## References

- [ADR-1508](1508-docs-site-toolchain-and-charts.md), [ADR-1510](1510-adr-nav-collapse-behind-index.md).
- Material for MkDocs 9.7.7: `material/plugins/search/plugin.py` (`search.exclude`), `material/plugins/meta/plugin.py` (`.meta.yml`), read from the installed package on 2026-10-04.
- Zensical, [Site search: search exclusion](https://zensical.org/docs/setup/search/#search-exclusion) and [MkDocs plugins: meta](https://zensical.org/docs/compatibility/mkdocs/plugins/#meta), fetched 2026-10-04; a Zensical 0.0.67 build of the test fixture left the planted records out of `search.json`.
- Source: `Q2026-10-04` popup answer: "User pages only (Recommended)".
