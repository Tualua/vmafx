- **The documentation site redesign has a decided toolchain.** The site stays
  on MkDocs 1.6.1 with Material for MkDocs 9.7.x, with the design built in
  Material's CSS layer, and moves to Zensical once it meets measured exit
  criteria, before Material's end of life on 2027-05-05. Charts are Vega-Lite
  specs beside repository data, diagrams use the `tools/figures/` engine, and
  prose is to be set at 60 to 75 characters per line (81 to 88 today). The ADR
  pages and tag pages leave the sidebar and are reached through the ADR index
  and tag pages
  ([ADR-1508](docs/adr/1508-docs-site-toolchain-and-charts.md),
  [ADR-1510](docs/adr/1510-adr-nav-collapse-behind-index.md),
  [Research-2137](docs/research/2137-docs-site-toolchain-charts-diagrams.md)).
