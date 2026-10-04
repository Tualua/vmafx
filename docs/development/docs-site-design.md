# Documentation site design

The documentation site is built with MkDocs and Material for MkDocs. Its look
comes from one stylesheet on top of Material,
[`docs/stylesheets/vmafx.css`](../stylesheets/vmafx.css), and two fonts the
site serves itself. There are no template overrides, so the same design renders
in Zensical's `classic` variant when the site moves there
([ADR-1508](../adr/1508-docs-site-toolchain-and-charts.md)).

This page is for people who change the site's look or write a page that uses
its components. Writing ordinary pages needs nothing from it: Markdown,
tables, code blocks and admonitions are styled automatically.

## Preview a change

Install the docs toolchain from its hash lock and serve the site:

```bash
python3 -m venv .venv-docs
.venv-docs/bin/pip install --require-hashes -r requirements/locks/package-build.txt
.venv-docs/bin/pip install --no-build-isolation --require-hashes -r docs/requirements-lock.txt
.venv-docs/bin/mkdocs serve
```

The site switches between light and dark with the toggle in the header and
follows the system setting on a first visit. Check a change in both, at a
desktop width and at a phone width (390 px). Before pushing, the pre-push hook
runs `mkdocs build --strict`
([pre-push hook](pre-push-mkdocs-strict.md)).

## Where the design lives

| Part | File | What it holds |
| --- | --- | --- |
| Stylesheet | `docs/stylesheets/vmafx.css` | colour and font custom properties for both schemes, typography, the prose measure, tables, code, admonitions, tabs, buttons, cards, the landing page |
| Site configuration | `mkdocs.yml` | `extra_css` lists the stylesheet; both palettes set `primary: custom` and `accent: custom`; `font: false` stops Material from loading Google Fonts |
| Fonts | `docs/assets/fonts/inter/`, `docs/assets/fonts/jetbrains-mono/` | the font files, the upstream licence and a `vendor.json` per family |
| Charts | `docs/charts/`, `scripts/docs/generate-charts.py`, `docs/javascripts/charts.js` | the Vega-Lite specs, their generator and the script that makes them interactive (see [Charts](#charts)) |
| Diagrams | `docs/figures/`, `docs/assets/figures/`, `tools/figures/` | the figure specs, their renders and the engine (see [Diagrams](#diagrams)) |
| Landing page | `docs/index.md` | the page text inside the `vx-*` wrappers described below |

The stylesheet's sections follow the order of the list above. Every colour is a
custom property: the brand ink of the header, tabs and footer (`--vx-ink`) is
shared, and each scheme sets its own text, surface, line, link and status
colours (`[data-md-color-scheme="default"]` for light,
`[data-md-color-scheme="slate"]` for dark). Material's own properties
(`--md-default-fg-color`, `--md-code-hl-*-color`, ...) are mapped onto these,
so Material's components follow the palette without further rules.

## Components for pages

### Prose and the measure

Paragraphs, list items, definition lists and block quotes stop at
`--vx-measure` (33 em), which holds a median of about 65 characters per line.
Tables, code blocks and figures use the whole column. A list item that holds a
code block or a table gives up the measure so the block keeps the column width;
its own paragraphs keep theirs.

### Admonitions

Write admonitions the usual way:

```markdown
!!! warning "This metric was removed"
    Text of the admonition.
```

`note`, `info` and `abstract` are blue; `tip` and `success` green; `warning`
amber; `failure`, `danger` and `bug` red; `question` and `example` violet;
`quote` grey. Each has a tinted background, a coloured left edge and its icon;
the text stays in the page's text colour.

### Landing page

`docs/index.md` uses `md_in_html` wrappers. Keep them when the page's text
changes:

| Wrapper | Holds |
| --- | --- |
| `<div class="vx-hero" markdown>` | the title, the opening paragraph, the two buttons (`{ .md-button .md-button--primary }`) and, in `vx-hero__command`, the first command |
| `<div class="vx-steps" markdown>` | the numbered "Start here" list, drawn as a row of steps |
| `<div class="grid cards vx-backends" markdown>` | one card per backend: a link, then an indented line with a short note |
| `<div class="vx-topics" markdown>` | the topic tables, which take the full width |

The front matter `hide: [navigation, toc]` gives the landing page the whole
width.

## Charts

A chart is a Vega-Lite spec beside its data, rendered at docs-generation time
([ADR-1508](../adr/1508-docs-site-toolchain-and-charts.md)). Readers see a
static SVG and a data table; with JavaScript the chart turns interactive and
shows exact values on hover. The data comes from repository files, never from
numbers copied into a page.

| Chart | Data | Pages |
| --- | --- | --- |
| `twin-exactness`: each GPU twin against the CPU extractor | `scripts/ci/exact_twins.d/`, `LIBM_TWINS` in `scripts/ci/cross_backend_calibration.py` | the landing page, [Backends](../backends/index.md) |
| `upstream-parity-allowlist`: recorded differences from Netflix/vmaf by extractor | `scripts/ci/upstream_parity.d/` | [Upstream parity guard](upstream-parity.md) |
| `per-frame-vmaf`: per-frame VMAF of the 576x324 snapshots | `testdata/scores_cpu_576.json`, `testdata/scores_sycl_*_576.json` | [Netflix benchmark baselines](netflix-benchmark-baselines.md) |

### How a chart is built

`scripts/docs/generate-charts.py` reads each chart's hand-written spec,
`docs/charts/<slug>/spec.vl.json`, builds its rows from the sources above and
writes:

| Output | What it is |
| --- | --- |
| `docs/charts/<slug>/data.json` | the rows |
| `docs/assets/charts/<slug>.light.svg`, `.dark.svg` | static renders by vl-convert-python, pinned in `docs/requirements-lock.txt` |
| the block between `<!-- >>> CHART <slug>` and `<!-- <<< CHART <slug> -->` on each page | the two images (`#only-light`, `#only-dark`), the caption and the data table |
| `docs/assets/charts/theme.json` | the light and dark Vega configs for the interactive chart |
| `docs/assets/charts/manifest.json` | the hash of every render and of the inputs it came from |
| `docs/javascripts/vendor/vega/vega-bundle.js` | vl-convert's bundle of Vega 6.2.0, Vega-Lite 6.4.1 and vega-embed 7.0.2, with `THIRD-PARTY-LICENSES.txt` and `vendor.json` |

`make docs-fragments-write` runs it; `make docs-fragments-check` fails when an
output differs. With vl-convert-python installed (the docs lock), the check
renders every SVG and the bundle again and compares them byte for byte.
Without it, the check says so and compares them with the manifest, which also
catches a spec or data edit that was not rendered. The two docs CI jobs run
`python3 scripts/docs/generate-charts.py --check --require-render`, which
refuses to run without the renderer. The static renders measure their text
with Liberation Sans, which vl-convert carries, so they are the same on every
machine; a clean Debian container produces the same bytes.

`docs/javascripts/charts.js` runs on every page and does nothing unless the
page holds a chart. Then it loads the bundle, mounts vega-embed in place of the
image with the site font and the current scheme's colours, and renders again
when the reader toggles the scheme. Material's instant navigation runs it
again on every page change.

### Add a chart

1. Write `docs/charts/<slug>/spec.vl.json` with `"data": {"name": "table"}`.
   Colours are tokens (`"@vx/cat-1"`, `"@vx/ink-2"`, ...) from `TOKENS` in
   the generator, so one spec serves both schemes.
2. Add a `Chart` entry to `CHARTS` in `scripts/docs/generate-charts.py` with a
   function that builds the rows from repository files, one that writes the
   data table, one that writes the text alternative from the rows, and the
   pages that show it.
3. Put the two sentinel lines on each page, then run
   `make docs-fragments-write`.

Charts follow the house chart method: pick the form first, one axis, thin
marks, a legend for two or more series with direct labels where they fit, and
colours checked with the palette validator against both surfaces (the
categorical steps and the twin chart's two ordinal steps pass). Vega-Lite has
no pattern fills, so a categorical chart keeps to three or four series. The
interactive chart has no keyboard focus on marks; the data table below each
chart carries the same values. Throughput and benchmark charts wait for RC7
evidence (`AGENTS.md` §11).

## Diagrams

Architecture, pipeline and flow diagrams are figure specs drawn with the figure
engine in `tools/figures/` ([README](https://github.com/VMAFx/vmafx/blob/master/tools/figures/README.md),
[ADR-1508](../adr/1508-docs-site-toolchain-and-charts.md)). A page names a
figure with a fence that holds only its slug:

````markdown
```figure
backend-dispatch
```
````

The MkDocs hook `tools/figures/mkdocs_hook.py` turns the fence into the
figure: an animated player with one tab per scenario and narration, a static
SVG for reduced motion and for readers without JavaScript, the caption and a
text description. The player takes its colours from the site palette, so a
figure follows the light and dark toggle.

| Figure | Page |
| --- | --- |
| `tiny-ai-pipeline` | [Tiny AI overview](../ai/overview.md) |
| `backend-dispatch` | [Backends](../backends/index.md) |
| `test-gates` | [Cross-backend gate](cross-backend-gate.md#relationship-to-other-gates) |
| `merge-release-flow` | [Release](release.md#automation-flow) |
| `tester-kit-flow` | [Tester image](../usage/tester-image.md) |
| `phase4b-platform` | [Phase 4b platform](../architecture/phase4b-distributed-platform.md) |
| `operator-reconcilers` | [Operator](operator.md#architecture) |
| `controller-job-lifecycle` | [Controller](../server/controller.md#job-lifecycle) |

### Add or change a figure

1. Read the code the figure shows. Write `docs/figures/<slug>.ts` (the spec
   type is `tools/figures/types.ts`); every `evidence` entry is a
   `path:Symbol` anchor into that code.
2. Render it: `node tools/figures/build.mjs build` writes
   `docs/assets/figures/<slug>.svg`, `.static.svg` and `.json`.
3. Put the fence on the page and commit the spec with its three outputs.

`make docs-figures` runs `build.mjs check` (the outputs match a fresh render)
and `build.mjs sources` (every evidence anchor still exists, every fence names
a figure). A spec stays within 40 boxes, 80 edges and 12 steps; a diagram
beyond that, or a sequence diagram, is a D2 source committed with its SVG. The
site has no Mermaid: the one Mermaid diagram became `phase4b-platform`, and
Material no longer loads the Mermaid script from a CDN.

`mkdocs.yml` keeps the specs out of the published pages (`/figures/` in
`exclude_docs`) and writes its `exclude_docs` patterns without `**`, which the
`sources` check does not parse.

## Navigation

The sidebar lists the topic pages and, under `ADRs`, three entries: the ADR
index, the template and the tag index
([ADR-1510](../adr/1510-adr-nav-collapse-behind-index.md)). Individual ADRs
and tag pages are reached through those two indexes, links and search; every
one is still built. Material renders the whole navigation into every page,
so taking the ADR and tag links out of it shrank every page: the sidebar of
`usage/cli/` went from 2,134 links (1,843 into `adr/`) to 294 (3). Measured on
strict builds of this tree before and after the change:

| | Before | After |
| --- | ---: | ---: |
| HTML pages built | 2,923 | 2,923 (the same paths) |
| Files in the site | 3,041 | 3,041 (the same paths) |
| Median page, bytes | 374,442 | 70,520 |
| All HTML pages | 1,116 MB | 223 MB |
| Landing page, bytes (gzipped) | 378,259 (48,144) | 76,402 (13,722) |
| `usage/cli/`, bytes (gzipped) | 470,715 (68,330) | 157,818 (33,729) |
| `mkdocs build --strict` | 274 s | 72 s |

A new ADR therefore needs no `mkdocs.yml` edit.
`scripts/docs/tests/test_generators.py` holds the three entries
(`test_adr_navigation_is_collapsed`).

## Fonts

The site serves Inter 4.1 for text and JetBrains Mono 2.304 for code, both
under the SIL Open Font License 1.1, from `docs/assets/fonts/`. No page loads a
font from another host. Each family's `vendor.json` names the release archive
and its SHA-256, the archive member each file comes from, the Unicode ranges
the fonts are subset to, and the SHA-256 of every committed file. Subsetting
keeps the variable weight axis and every OpenType feature; a character outside
the ranges falls back to the reader's system font.

To update a font, download the release named in a new `source`, set
`source_sha256` and the members' `from_sha256`, then write the files:

```bash
pip install fonttools==4.60.1 brotli==1.1.0
python3 scripts/docs/vendor_fonts.py --manifest docs/assets/fonts/inter/vendor.json \
  --archive Inter-4.1.zip
```

`--check` rebuilds the files from the archive and fails when one differs from
the committed copy. Without the archive,
`python3 scripts/docs/check_vendored_assets.py` (part of
`make docs-fragments-check`) fails when a committed file differs from its
recorded hash, is missing, or is not listed, or when the licence file is not
listed. `scripts/docs/tests/test_vendored_assets.py` holds both tools to that
contract.

## Typography and contrast, measured

Measured on a strict build in headless Chromium at 1440 px, in both schemes,
by reading computed styles and counting the characters on every full rendered
line of the first 60 prose blocks of a page.

| Property | Target (ADR-1508) | Measured |
| --- | --- | --- |
| Characters per line | 60 to 75, about 66 | `usage/cli/`: median 65, longest 75; `getting-started/`: median 64, longest 72 |
| Body text | 16 px or larger, line height 1.6 | Inter 16 px, line height 25.6 px (1.6); 17.6 px from 1600 px wide |
| Headings | weight 600 or more, full text colour, scale near 1.25 | h1 31.25 px weight 700, h2 25 px weight 650, h3 20 px weight 600, all in the text colour |
| Spacing around an h2 | about 2 em above, 0.6 em below | 50 px above (2 em of the h2), 15 px below |
| Tables, admonitions | 14 to 15 px | 14.5 px, line height 1.5 |
| Code | 14 px, line 1.5 | JetBrains Mono 14 px, line height 21 px (1.5) |
| WCAG 2.2 SC 1.4.12 | no loss of content | with line height 1.5, paragraph spacing 2 em, letter spacing 0.12 em and word spacing 0.16 em forced, no element clips its text and no page scrolls sideways, on five pages in both schemes at 1440 px and 390 px |

Contrast ratios (WCAG 2.2: 4.5 for text, 3 for large text and interface
parts), measured on the rendered pages:

| Element | Light | Dark |
| --- | --- | --- |
| Body text, headings, table cells | 17.85 | 14.89 |
| Admonition text | 16.21 | 13.47 |
| Links in prose | 6.70 | 12.66 |
| Code block text | 13.40 | 14.04 |
| Code comments (lowest code token) | 5.82 | 6.25 |
| Sidebar and table-of-contents links | 6.70 to 7.58 | 8.77 to 12.66 |
| Header, tabs and footer text | 11.50 to 18.10 | 9.76 to 18.65 |
| Landing hero text and links | 10.43 to 11.85 | 9.80 to 11.12 |
| Focus ring, button border (interface) | 5.17, 6.70 | 12.66 |
| Admonition edge and icon on its tint (interface) | 4.36 to 5.75 | 6.10 to 9.75 |

## Zensical

Everything here is CSS, Python Markdown (`md_in_html`, `attr_list`) and
`mkdocs.yml` settings that Zensical reads (`extra_css`, `primary: custom`,
`font: false`, `hide`), except the figures: Zensical does not run MkDocs hooks
yet, so until it does a Zensical build shows each `figure` fence as a code
block. `node tools/figures/build.mjs portable` replaces the fences with the
static images for that case (ADR-1508, exit criteria).

A build of the documentation with Zensical 0.0.67 renders the landing page
and the content pages with the same layout, colours and measure (median 66 to
67 characters per line). Its `classic` variant gives buttons a background of
their own, so the stylesheet sets `background-color: transparent` on
`.md-button` explicitly.
