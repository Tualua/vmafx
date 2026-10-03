<!-- markdownlint-disable MD013 MD060 -->
# ADR-1510: Collapse the ADR navigation behind the index and tag pages

- **Status**: Accepted; supersedes the sidebar enumeration of [ADR-0937](0937-mkdocs-nav-decade-buckets.md)
- **Date**: 2026-10-03
- **Deciders**: Lusoris
- **Tags**: docs, mkdocs, navigation, adr, fork-local

## Context

[ADR-0937](0937-mkdocs-nav-decade-buckets.md) put every ADR and every ADR tag
page into the site's sidebar, in per-hundred groups written by
`scripts/docs/generate-adr-nav.sh`. Material for MkDocs renders the whole
navigation into every page. On a strict build of the tree at `1b1663830`
([Research-2137](../research/2137-docs-site-toolchain-charts-diagrams.md)):

- `usage/cli/index.html` is 456 KB, of which the primary navigation is 364 KB
  (80 %). 1,839 of its 2,100 navigation links point into `adr/`, 628 of them to
  tag pages.
- On `adr/0403-mkdocs-strict-gate-validation-policy/index.html` the navigation
  is 94 % of the page.

Every merged ADR also rewrites the generated block in `mkdocs.yml`, so parallel
ADR branches touch the same lines.

## Decision

We will take the individual ADR pages and ADR tag pages out of the sidebar. The
`ADRs` entry lists three pages: `Overview` (`adr/README.md`, the index of every
ADR), `Template` (`adr/0000-template.md`) and `By tag` (`adr/by-tag/index.md`).
Readers reach an ADR through the index, a tag page, a link or site search.

This replaces these statements of ADR-0937, whose body stays as written:

- Decision item 1, that `generate-adr-nav.sh` emits per-hundred collapsible
  groups of ADRs under `- ADRs:`. The three entries become static lines in
  `mkdocs.yml`. `generate-adr-nav.sh`, its `--check` and `--write` calls in
  the `Makefile` and its cases in `scripts/docs/tests/test_generators.py` are
  removed in the implementing pull request.
- The list under "The `ADRs:` top-level nav entry surfaces", where it adds one
  group per bucket and a `By tag` group with one entry per tag.
- Its consequence that ADRs are "discoverable from the material-theme left
  rail". They are discoverable through the two index pages and search.

ADR-0937's tag pages stay: `generate-adr-by-tag.sh` keeps writing
`docs/adr/by-tag/` and `make docs-fragments-check` keeps checking it. The
`validation.nav.omitted_files: info` setting already covers ADR pages left out
of the navigation; its comment in `mkdocs.yml` is updated to cite this record.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Collapse behind the index and tag pages (chosen) | navigation shrinks to the non-ADR pages on every page; ADR merges stop touching `mkdocs.yml`; one generator fewer; works the same under Zensical | ADRs leave the sidebar; readers go through the index or search | — |
| Keep ADR-0937's full enumeration | ADRs browsable from the sidebar | 80 to 94 % of each page is navigation; every ADR merge edits `mkdocs.yml` | the cost every reader pays on every page |
| Material's `navigation.prune` | keeps the enumeration; Material states it cuts the built site's size by "33% or more" | "not compatible with `navigation.expand`", which this site uses; a Material feature with no stated Zensical equivalent; `mkdocs.yml` churn stays | changes the navigation's behaviour without removing the churn, and may not survive the generator move of [ADR-1508](1508-docs-site-toolchain-and-charts.md) |
| Keep the per-hundred groups, drop only the tag entries | removes 628 links | about 1,200 ADR links stay on every page; churn stays | half the cost remains |

## Consequences

- **Positive**: pages lose most of their navigation weight; ADR branches no
  longer conflict in `mkdocs.yml`; the docs fragment gate has one generator
  fewer.
- **Negative**: the sidebar no longer shows the project's decision history by
  number range; the per-hundred bucket labels go away with the generator.
- **Neutral / follow-ups**: the implementing pull request belongs to the site
  redesign lane. It edits `mkdocs.yml`, the `Makefile`, the generator tests
  and the ADR workflow page (`docs/development/adr-workflow.md`), and measures
  page size before and after.

## References

- [ADR-0937](0937-mkdocs-nav-decade-buckets.md): the navigation this record partly supersedes.
- [ADR-1508](1508-docs-site-toolchain-and-charts.md) and [Research-2137](../research/2137-docs-site-toolchain-charts-diagrams.md): the redesign decision and the page-size measurements.
- Material for MkDocs, [Setting up navigation: navigation pruning](https://squidfunk.github.io/mkdocs-material/setup/setting-up-navigation/), fetched 2026-10-03.
- Source: `Q2026-10-03` (ADR-1508 open choices), ADR navigation answer, verbatim: "Collapse behind index (Recommended)".
