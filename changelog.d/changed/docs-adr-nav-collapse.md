- **The documentation sidebar no longer lists every ADR.** The `ADRs` entry
  now holds the ADR index, the template and the tag index; individual ADRs and
  tag pages are reached from those pages, from links and from search
  ([ADR-1510](docs/adr/1510-adr-nav-collapse-behind-index.md)). Every page
  stays on the site, and each page is far smaller because the navigation it
  carries shrank: see
  [Documentation site design](docs/development/docs-site-design.md#navigation).
  `scripts/docs/generate-adr-nav.sh` is retired, so a new ADR no longer edits
  `mkdocs.yml`.
