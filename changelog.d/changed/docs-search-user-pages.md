- **The documentation search covers user pages only.** ADR bodies, research
  digests, the rebase notes, the state ledger and the changelog archive leave
  the search index, which shrinks from 29.3 MB (8.9 MB gzipped) to 5.8 MB
  (1.7 MB gzipped) and loads on every first page view. ADR and research titles
  stay findable through the indexes, the ADR tag pages and two new title lists
  (`docs/adr/titles.md`, `docs/research/titles.md`); record text is searched
  with GitHub code search
  ([Documentation site design](docs/development/docs-site-design.md#search),
  [ADR-1512](docs/adr/1512-docs-search-user-pages-only.md)).
