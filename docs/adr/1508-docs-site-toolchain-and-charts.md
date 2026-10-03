<!-- markdownlint-disable MD013 MD060 -->
# ADR-1508: Documentation site generator, charts, diagrams and typography for the redesign

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: Lusoris
- **Tags**: docs, mkdocs, design, navigation, tooling, fork-local

## Context

The documentation site on GitHub Pages is to be redesigned: its own palette,
typography, landing page and components, interactive charts from data files in
the repository, and architecture and pipeline diagrams redrawn as SVG that read
in light and dark. The maintainer decided to settle the generator first, so the
design is built once, and added that the docs read poorly and contain very long
text blocks.

The site is built with MkDocs 1.6.1 and Material for MkDocs 9.7.7. The hosted
Lint job prints the Material team's warning that MkDocs 2.0 removes plugins,
rewrites theming and offers no migration path. The investigation behind this
record is [Research-2137](../research/2137-docs-site-toolchain-charts-diagrams.md).
Its main findings:

- MkDocs 1.x has had no release since 2024-08-30 and closed its repository to
  outside contributions on 2026-10-02. MkDocs 2.0 is in pre-release
  (`2.0.dev6`, 2026-09-15) and cannot build Material sites. The docs lock pins
  `mkdocs>=1.6.1,<2`, so its release does not break this build.
- Material for MkDocs is in maintenance mode, with critical fixes until
  2027-05-05. Its successor Zensical (MIT, by the same team) reads `mkdocs.yml`,
  renders the same HTML in its `classic` variant, and builds this tree in 34 s
  against MkDocs' 257 s. It is alpha until 0.1.0 on 2026-11-05 and does not yet
  support `hooks` or `exclude_docs`, has no `--site-dir` option, and expresses
  link validation with keys MkDocs 1.6 does not accept: on this tree its strict
  build stops on 987 link warnings that ADR-0403 keeps at `info`.
- Astro Starlight and Docusaurus are capable and MIT-licensed, but moving there
  means front matter for 4,122 pages (Starlight), a strategy for 21,407 relative
  `.md` links (Starlight neither rewrites nor validates them), a new navigation
  generator, and new strict-build and pre-push gates.
- The site renders 81 to 88 characters per line at common desktop widths, above
  the 45 to 75 range typographic guidance gives and above WCAG 2.2 SC 1.4.8's
  80. Tables and admonitions are set at 12.8 px, and h1 and h2 at weight 300 in
  a light grey.

## Decision

We will keep the site on MkDocs 1.6.1 and Material for MkDocs 9.7.x for the
redesign, build the design in Material's CSS layer, and move to Zensical when
it meets the exit criteria below, before 2027-05-05. We will draw charts with
Vega-Lite from JSON specs and data files in the repository, and diagrams with
the figure engine already in `tools/figures/`. The ADR navigation is collapsed
behind the ADR index and tag pages, recorded separately in
[ADR-1510](1510-adr-nav-collapse-behind-index.md) because it partly supersedes
[ADR-0937](0937-mkdocs-nav-decade-buckets.md). The typography targets in the
table below are the acceptance bar for the redesign. The maintainer chose the
generator path, the chart library and the navigation on 2026-10-03; the
choices and their reasons are in [Choices made](#choices-made).

### Generator

- The design lives in one `extra_css` stylesheet that sets Material's colour
  and font custom properties and overrides its size selectors, plus template
  overrides only where markup must change (the landing page). Overrides start
  from Material 9.6.18 or later templates and call no Python, so Zensical's
  MiniJinja renders them unchanged.
- The `mkdocs` pin stays `<2`.
- **Exit criteria for Zensical**, all measured on a CI job that builds this tree
  with both generators: Zensical 0.1.0 or later; the same 2,912 pages with no
  fragment leak; the figure hook running (or `build.mjs portable` accepted as the
  fallback); a validation configuration that fails on broken anchors and keeps
  missing-page links informational; the pre-push hook and both required docs
  jobs switched. The switch is reviewed in February 2027, leaving three months
  before Material's end of life.

### Charts

- A chart is a Vega-Lite spec beside its data (JSON or CSV under `docs/`). A
  generator under `scripts/docs/`, run by `make docs-fragments-write`, renders
  each spec to a light and a dark static SVG with `vl-convert-python` and writes
  the data table; `make docs-fragments-check` fails when either drifts.
- The page shows the static SVG and the table without JavaScript. A page script,
  re-run on Material's `document$` and on a palette change, mounts `vega-embed`
  for hover, crosshair and exact values, only on pages that hold a chart.
- Charts follow the maintainer's chart method (form first, validated palette,
  thin marks, legend and direct labels, one axis, table view). Vega-Lite has no
  pattern fills, so a categorical chart keeps to four series or fewer, labelled
  directly.
- Throughput charts wait for RC7 evidence (`AGENTS.md` §11).

### Diagrams

- Architecture, pipeline and flow diagrams are figure specs under
  `docs/figures/`, rendered and checked by `tools/figures/` (`make docs-figures`).
  `mkdocs.yml` lists `tools/figures/mkdocs_hook.py`.
- A diagram the engine cannot express (more than 40 boxes or 80 edges, or a
  sequence diagram) is a D2 source committed with its rendered SVG.
- The one Mermaid diagram is redrawn as a figure, after which the Mermaid custom
  fence is removed, and with it the runtime script Material loads from unpkg.

### Typography targets

| Property | Today (measured) | Target |
| :--- | :--- | :--- |
| Characters per line in prose | median 81 to 88, up to 93 | 60 to 75, about 66: a `max-width` near 32 to 34 em on prose blocks (not `ch`: in Inter `1ch` is 10.1 px against a 7.6 px average character) |
| Body text | Inter 16 px, line height 1.6 | 16 px or larger, line height 1.6 |
| Paragraph spacing | 1 em | 1 em; about 2 em above an h2 and 0.6 em below |
| Headings | h1 32 px weight 300 light grey, h2 25 px weight 300, h3 20 px weight 400 | weight 600 or more in the full text colour, scale near 1.25 |
| Tables, admonitions | 12.8 px | 14 to 15 px; wide tables scroll in their own container |
| Code | 13.6 px, code-block line 1.4 | 14 px, line 1.5 |
| Text spacing | not tested | passes a WCAG 2.2 SC 1.4.12 override test |

Splitting long paragraphs (151 above 150 words) is content work outside this
record.

## Choices made

The proposal left three choices open. The maintainer took the recommended
option of each on 2026-10-03 (popup answers quoted in
[References](#references)).

| Choice | Chosen | Not chosen | Why |
| :--- | :--- | :--- | :--- |
| Generator path | Material now, Zensical before 2027-05-05 | Zensical now (alpha software in two required jobs, no figure hook, 987 link warnings, pre-push hook rewritten); Astro Starlight (front matter for 4,122 pages, 21,407 links, navigation generator, strict gate and pre-push hook rebuilt, a Node lockfile) | no migration now; the design is built once, on the HTML both MkDocs and Zensical render; content and gates stay as they are |
| Chart library | Vega-Lite | Apache ECharts (220 KB with a Node bundling step to 368 KB gzipped, Apache-2.0 NOTICE with the copy) | specs are data files beside the data; static SVG comes from the Python docs lock on Linux, macOS and Windows; about 303 KB gzipped, loaded only on chart pages |
| ADR navigation | collapse behind the index and tag pages ([ADR-1510](1510-adr-nav-collapse-behind-index.md)) | keep every ADR and tag page in the sidebar ([ADR-0937](0937-mkdocs-nav-decade-buckets.md)) | the navigation is 80 to 94 % of each page's bytes, and 1,839 of 2,100 navigation links point into `adr/`; ADR merges stop editing `mkdocs.yml` |

## Alternatives considered

### Generator

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Stay on Material 9.7.x, design in its CSS layer, Zensical later (chosen) | no migration now; the design carries to Zensical's `classic` variant; ADR-0403, ADR-0466 and ADR-0937 tooling unchanged | both upstreams in maintenance until a switch; 257 s local and 562 s hosted builds; 29 MB search index | — |
| Switch to Zensical now | 34 s builds measured; same team; reads `mkdocs.yml` | alpha; `hooks`, `exclude_docs` and `--site-dir` unsupported; strict build fails on 987 link warnings; validation keys incompatible with MkDocs 1.6 | the gaps hit the figure engine and both required docs jobs; revisit after 0.1.0 |
| Astro Starlight | CSS custom properties for width and type; Pagefind; figure engine and praetor preset support it | `title` front matter required on 4,122 pages; 21,407 relative links not rewritten and not validated; navigation, gates and hook rebuilt; Node toolchain | migration cost far above a styling goal; build time on this tree unmeasured |
| Docusaurus | rewrites relative `.md` links; `onBrokenLinks` gate; Mermaid plugin | React and Infima theming; navigation and gates rebuilt; no figure-engine integration; build time unmeasured | same cost class as Starlight with less fleet tooling |
| ProperDocs (MkDocs 1.x fork) | keeps MkDocs 1.x behaviour | renamed config; Material's compatibility uncertain; last push 2026-07-19 | no maintained theme path |
| MkDocs 2.0 | maintained by its new owner | no plugins, new theming, TOML config, no migration path | cannot build this site |

### Charts

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Vega-Lite (chosen) | JSON spec beside the data; schema-checkable; build-time SVG through `vl-convert-python` (BSD-3, abi3 wheels); `description` becomes `aria-label`; tooltips escape HTML | 303 KB gzipped; no pattern fills; theme toggle needs a re-render; no keyboard focus on marks | — |
| Apache ECharts | pattern fills, generated aria description, `setTheme()`, SVG renderer and server-side SVG | heaviest; a custom build needs a Node bundler; Apache NOTICE | runner-up; preferred if a chart needs pattern fills |
| Observable Plot | SVG; colours accept CSS variables, so no re-render on toggle; 139 KB | JavaScript specs; server-side render needs a DOM shim; last release 2025-02-14 | specs are code, not data; slow release cadence |
| Chart.js | 70 KB; mature | canvas: no CSS theming, text alternative added by hand | fails the SVG and theming rows |
| uPlot | 22 KB; fast time series | canvas; no accessibility; few chart forms | fails the accessibility rows |
| Matplotlib (already a harness dependency) | no new dependency | static images, no hover | the brief asks for interactive charts |

### Diagrams

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| `tools/figures/` figure engine (chosen) | already adopted and checked; evidence anchors held against the code; static SVG, text description, reduced motion; follows the site palette | limits per figure; needs the MkDocs hook; the static SVG follows the system theme, not the site toggle | — |
| D2 | text sources; dark theme; no size limits | MPL-2.0 Go binary; no checks in the repository | kept for diagrams beyond the engine's limits |
| Mermaid, themed by Material | works today | 976 KB gzipped runtime from a third-party CDN; little layout control | the engine is already adopted and lighter |
| Hand-written SVG | full control | slow to change; no checks | maintenance cost |
| Excalidraw | quick sketches | JSON scenes are poor diffs; hand-drawn look | does not fit reference diagrams |

## Consequences

- **Positive**: the design is built once, on HTML that both MkDocs and Zensical
  render. Charts and diagrams are text in the repository with drift checks, and
  each has a static form and a text alternative. The typography targets are
  measurable.
- **Negative**: the site stays on upstreams in maintenance mode until the
  switch. The redesign adds a CSS file and template overrides to maintain across
  a generator change. Charts add a 33 MB wheel to the docs lock and a vendored
  runtime script.
- **Neutral / follow-ups**:
  - a dual-build CI job (MkDocs and Zensical) that reports the exit criteria;
  - a Renovate rule that keeps `mkdocs` below 2;
  - the chart generator under `scripts/docs/` with its negative test, the
    figure hook in `mkdocs.yml`, and the figure specs listed in Research-2137;
  - the ADR navigation collapse of [ADR-1510](1510-adr-nav-collapse-behind-index.md).

## Supply-chain impact

No dependency changes in this record. When the chart generator lands:

- **New dependencies**: `build`: `vl-convert-python` 1.9.0.post1
  (BSD-3-Clause, <https://github.com/vega/vl-convert>), hash-pinned in
  `docs/requirements-lock.txt`. `runtime`, on chart pages only: the Vega bundle
  of the same release (Vega 6.2.0, vega-embed 7.0.2, BSD-3-Clause), vendored.
- **Removed dependencies**: the Mermaid runtime Material loads from
  `https://unpkg.com/mermaid@11/dist/mermaid.min.js`, once the Mermaid fence is
  gone.
- **Build-time fetches**: none beyond the hash-locked `pip install`.

## References

- [Research-2137](../research/2137-docs-site-toolchain-charts-diagrams.md): sources, measurements and the commands that reproduce them.
- [ADR-0403](0403-mkdocs-strict-gate-validation-policy.md), [ADR-0466](0466-mkdocs-strict-pre-push-hook.md), [ADR-0937](0937-mkdocs-nav-decade-buckets.md).
- Material for MkDocs blog, [What MkDocs 2.0 means for your documentation projects](https://squidfunk.github.io/mkdocs-material/blog/2026/02/18/mkdocs-2.0/), fetched 2026-10-03.
- Zensical, [What changes on November 5, 2026](https://zensical.org/upcoming-changes), fetched 2026-10-03.
- W3C, [Understanding SC 1.4.8](https://www.w3.org/WAI/WCAG22/Understanding/visual-presentation.html) and [SC 1.4.12](https://www.w3.org/WAI/WCAG22/Understanding/text-spacing.html), fetched 2026-10-03.
- [ADR-1510](1510-adr-nav-collapse-behind-index.md): the navigation choice, which partly supersedes [ADR-0937](0937-mkdocs-nav-decade-buckets.md).
- Source: `Q2026-10-03` (site redesign), popup answer, verbatim: "Own design, toolchain checked first".
- Source: `Q2026-10-03` (ADR-1508 open choices), popup answers, verbatim: generator: "Material now, Zensical later (Recommended)"; charts: "Vega-Lite (Recommended)"; ADR navigation: "Collapse behind index (Recommended)".
- Source: `req` (paraphrased by the coordinator, 2026-10-03): "the readability of the docs is poor; some text blocks are huge".
