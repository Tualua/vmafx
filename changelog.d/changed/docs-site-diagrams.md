- **Diagrams in the documentation are figures checked against the code.**
  Eight architecture, pipeline and flow diagrams are drawn with the figure
  engine in `tools/figures/`: the tiny-AI pipeline, backend dispatch, the test
  gates, the merge and release flow, the outside-tester flow, the Phase 4b
  platform, the operator's reconcilers and the controller's job lifecycle. Each
  plays its scenarios in the browser, falls back to a static SVG, carries a text
  description, follows the site's light and dark palette, and names the code it
  was drawn from; `make docs-figures` fails when an anchor no longer exists. The
  ASCII-art diagrams of those pages and the one Mermaid diagram are gone, and
  the site no longer loads the Mermaid script from a CDN
  ([Documentation site design](docs/development/docs-site-design.md#diagrams),
  [ADR-1508](docs/adr/1508-docs-site-toolchain-and-charts.md)).
