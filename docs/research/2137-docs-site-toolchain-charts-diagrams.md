<!-- markdownlint-disable MD013 MD060 -->
# Research-2137: Documentation site generator, charts, diagrams and readability

- **Status**: Active
- **Workstream**: [ADR-1508](../adr/1508-docs-site-toolchain-and-charts.md), [ADR-1510](../adr/1510-adr-nav-collapse-behind-index.md)
- **Last updated**: 2026-10-03

## Question

The maintainer wants the documentation site on GitHub Pages redesigned: its own
palette, typography, landing page and components, interactive charts drawn from
data files in the repository, and the architecture and pipeline diagrams redrawn
as clean SVG in light and dark. The maintainer also reports that the docs read
poorly and that some text blocks are very long. The popup decision of 2026-10-03
was to settle the site generator before any styling, so the design is not done
twice.

This digest answers five questions:

1. Which generator should the design sit on: MkDocs with Material for MkDocs
   (today), its successor Zensical, or another generator?
2. Which chart library fits the site and the house chart style?
3. How should diagrams be drawn so that they read in both themes and stay
   reviewable as text?
4. Which charts and diagrams can the current content use right away?
5. What are today's typographic values, and what should they be?

Every fact below comes from a source fetched on 2026-10-03 or from a command run
on that date against `origin/master` at `1b1663830`. Sources are listed at the
end; measurements name the command that produced them.

## The short answer

| Question | Recommendation | Main reason |
| :--- | :--- | :--- |
| Generator | Stay on MkDocs 1.6.1 with Material 9.7.7 now; do the design in Material's CSS layer; move to Zensical once it passes the exit criteria below, before Material's end of life on 2027-05-05 | Zensical renders the same HTML as Material, so a design built on Material carries over. Astro Starlight or Docusaurus would mean re-plumbing 4,122 pages, 21,407 relative links and every docs gate for a styling goal. |
| Charts | Vega-Lite: a JSON spec beside a data file, static SVG rendered at docs-generation time with `vl-convert-python`, `vega-embed` loaded only on chart pages | The spec is data, so it diffs and validates like the rest of the generated docs. The static render runs in the Python toolchain the docs already lock, on Linux, macOS and Windows. |
| Diagrams | The figure engine the repository already carries in `tools/figures/` (interfig, MIT), with D2 for the few diagrams it cannot express | It is adopted, checked by `make docs-figures`, follows the site palette, ships a static SVG and a text description, and checks every diagram against the code it cites. |

The maintainer took the recommended option of each open choice on 2026-10-03:
Material now and Zensical later, Vega-Lite, and the ADR navigation collapsed
behind the index and tag pages. ADR-1508 records the choices in
[Choices made](../adr/1508-docs-site-toolchain-and-charts.md#choices-made);
the navigation change is [ADR-1510](../adr/1510-adr-nav-collapse-behind-index.md).

## Findings

### 1. Generator

#### State of MkDocs

- MkDocs 1.6.1, released 2024-08-30, is the latest stable release on PyPI. The
  2.0 line is in pre-release: `2.0.dev2` appeared on 2026-08-29 and `2.0.dev6` on
  2026-09-15 (PyPI JSON API).
- The Material team's analysis of 2.0 (blog post of 2026-02-18, last updated
  2026-10-02) lists the breaking changes: no plugin system, a rewritten theming
  system that passes navigation as pre-rendered HTML, TOML configuration instead
  of `mkdocs.yml`, and "There is no migration path for existing projects." It
  adds that "A release date has not been announced".
- The same post records that the MkDocs 1.x repository was closed to outside
  issues and pull requests on 2026-10-02, and calls MkDocs 1.x unmaintained.
- MkDocs 2.0 is now MIT-licensed; the post notes the licence might change again.
- The warning in the hosted Lint job comes from Material 9.7.2 or later and can
  be silenced with `NO_MKDOCS_2_WARNING=1`. It does not affect the build:
  `docs/requirements.txt` pins `mkdocs>=1.6.1,<2`, and Material 9.7.5 itself
  limits MkDocs to `<2`.

#### State of Material for MkDocs and Zensical

- Material for MkDocs 9.7.7 (2026-07-17, MIT) is the latest release. The
  Zensical announcement of 2025-11-05 put Material in maintenance mode, with
  critical bug and security fixes for at least 12 months.
- Zensical's "What changes on November 5, 2026" page extends that: "critical
  maintenance continuing until May 5, 2027". The same day Zensical 0.1.0 "will
  begin a dependable release line"; Zensical stays on 0.x after that.
- Zensical is MIT-licensed, built by the Material team, written in Rust and
  Python, and marked "Development Status :: 3 - Alpha" on PyPI. The latest
  release is 0.0.67 (2026-09-30). Studio (an editor) and Spark (a membership)
  are optional paid products; the generator stays free.
- Zensical reads `mkdocs.yml`, renders Python Markdown, and provides a `classic`
  theme variant that "preserves the appearance of Material for MkDocs, while
  both variants retain the same HTML structure". Material template overrides
  based on 9.6.18 or later "should generally work without changes".
- Zensical does not yet support these `mkdocs.yml` settings: `remote_branch`,
  `remote_name`, `exclude_docs`, `draft_docs`, `not_in_nav` and `hooks`. Its
  `build` command has no `--site-dir` option. Plugins it does not implement are
  silently ignored. It implements `search`, `minify` and, since 0.0.67, an
  `exclude` plugin.
- Zensical's `validation` block uses its own keys (`invalid_links`,
  `invalid_link_anchors`, ...), not MkDocs 1.6's `nav` / `links` keys that
  carry this repository's carve-outs ([ADR-0403](../adr/0403-mkdocs-strict-gate-validation-policy.md)).
  `zensical build --strict` aborts on any warning.

#### Zensical on this repository, measured

The tree at `origin/master` was copied to a scratch directory and built with
Zensical 0.0.67 from PyPI, without changing a page.

| Build | Wall time | Pages | Result |
| :--- | ---: | ---: | :--- |
| `mkdocs build --strict --site-dir <scratch>` (MkDocs 1.6.1, Material 9.7.7, the hash lock) | 257 s | 2,912 | passes; 2,673 INFO lines |
| `zensical build --strict`, `mkdocs.yml` unchanged | 34 s | — | aborts: 987 "page does not exist" warnings |
| `zensical build`, `mkdocs.yml` unchanged | 36 s | 4,123 | 1,210 pages from `adr/_index_fragments/` leak into the site, because `exclude_docs` is ignored |
| `zensical build` with an `exclude` plugin entry for the two `exclude_docs` globs | 34 s | 2,912 | same page count as MkDocs; 956 warnings |

The host is a 32-core workstation running at niceness 10; MkDocs uses one core.
The hosted Docs Site Build reported "Documentation built in 562.22 seconds"
(comment in `.github/workflows/docs.yml`).

The 987 warnings are links to `.md` files outside `docs_dir` or to renamed ADRs:
639 in `changelog-archive/`, 149 in `adr/`, 89 in `development/`. MkDocs keeps
this class at `info` by ADR-0403; ADR bodies are frozen once Accepted, so many
cannot be edited. In Zensical the equivalent is `invalid_links: false` with
`invalid_link_anchors: true`, which MkDocs 1.6 would not accept in the same file.

Zensical rendered the one Mermaid diagram and the 2,139-link navigation the same
way MkDocs did.

#### What today's build costs the reader

Measured on the MkDocs build above:

- Every page carries the whole navigation: 2,139 navigation links on the home
  page. The median page is 373 KB of minified HTML; on
  `adr/0403-mkdocs-strict-gate-validation-policy/` the article is 2.1 % of the
  page's bytes.
- The search index `search/search_index.json` is 29.1 MB (8.9 MB gzipped).
  Material's built-in search loads it when the reader first searches.
- Zensical's build of the same tree writes a 31.9 MB `search.json`.

These numbers do not decide the generator, but the redesign should fix them. The
navigation weight comes from the generated ADR navigation
([ADR-0937](../adr/0937-mkdocs-nav-decade-buckets.md)), which any generator would
render on every page unless the design collapses it.

#### Alternatives to the Material line

| | Material 9.7.7 on MkDocs 1.6.1 (today) | Zensical | Astro Starlight | Docusaurus | ProperDocs |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Version, date, licence | 9.7.7, 2026-07-17, MIT | 0.0.67, 2026-09-30, MIT; 0.1.0 announced for 2026-11-05 | 0.42.5, 2026-10-01, MIT (Astro 7.3.5, MIT) | 3.10.2, 2026-07-10, MIT | 1.6.7, 2026-03-20, BSD-2-Clause |
| Upstream status | Critical fixes only, until 2027-05-05; MkDocs 1.x unmaintained | Active, alpha | Active, pre-1.0 | Active | MkDocs 1.x fork; last push 2026-07-19 |
| Reads `mkdocs.yml` and today's pages | yes | yes, with the gaps above | no | no | renamed config (`properdocs.yml`) |
| Relative `.md` links (21,407 in `docs/`) | rewritten to URLs and checked | rewritten and checked | not rewritten; `starlight-links-validator` errors on relative links or skips them | rewritten to URLs ("Docusaurus' Markdown loader will convert the file path to the target file's URL path") | rewritten and checked |
| Page front matter | optional | optional | `title` required by the content schema (`schema.js`) for all 4,122 pages | optional | optional |
| Diagrams today | Mermaid fence, Material themes it | Mermaid fence | plugin needed | `@docusaurus/theme-mermaid` | via Material |
| `tools/figures/` integration | MkDocs hook (`mkdocs_hook.py`) | none until `hooks` lands; `build.mjs portable` renders image fallbacks | Astro integration (`astro.mjs`) | none | MkDocs hook, if Material runs |
| Styling surface | colours and fonts as CSS custom properties; sizes in plain selectors overridden by `extra_css`; template overrides | same HTML and CSS as Material (`classic`), or the `modern` variant | CSS custom properties for width, type scale and colour (`props.css`), cascade layers, Astro components | Infima CSS variables, React component swizzling | Material's |
| Search | Lunr index, 29.1 MB here | own engine, 31.9 MB `search.json` here | Pagefind, chunked index ("under 300kB" on a 10,000 page site) | plugin | Material's |
| Build time on this tree | 257 s (562 s hosted) | 34 s | not measured | not measured | not measured |
| Migration cost here | none | config edits and the gaps above | front matter for 4,122 pages, link rewriting or a link transform plugin, content moved under `src/content/docs/`, navigation generator rewritten as a sidebar, strict gate and pre-push hook replaced, Node lockfile | link and Markdown mostly portable (`markdown.format: 'detect'`), sidebars rewritten, Infima and React theming, strict gate replaced, Node lockfile | config rename; Material's compatibility with an import-renamed fork is the Material team's stated concern |

Markdown portability is good in every direction: the docs use little Python
Markdown syntax beyond CommonMark and tables (`git grep` counts over
`docs/*.md`): 1 admonition, 5 content tabs, 23 footnotes, about 61
`md_in_html` attributes and no emoji shortcodes. The cost of leaving MkDocs is
in links, front matter, navigation and the gates, not in the prose.

#### MkDocs-specific tooling in this repository

Any move must carry these along:

- `mkdocs.yml`: 2,258 lines, with the sentinel-bounded ADR navigation block
  written by `scripts/docs/generate-adr-nav.sh` (ADR-0937), `exclude_docs`, and
  the `validation` carve-outs of ADR-0403.
- `make docs-fragments-check` and `make docs-fragments-write`: changelog, ADR
  index, ADR tag pages, ADR navigation, exact-twin table, upstream-parity
  allowlist, AGENTS index and hardware reports. Only the navigation generator is
  tied to MkDocs; the rest write Markdown.
- `mkdocs build --strict` in two required jobs (`Docs Site Build` in
  `docs.yml`, `Docs` in `lint-and-format.yml`) and in the pre-push hook
  `scripts/git-hooks/pre-push-mkdocs-strict.sh`
  ([ADR-0466](../adr/0466-mkdocs-strict-pre-push-hook.md)), which passes
  `--site-dir`.
- `make docs-figures` and `.github/workflows/praetor-docs.yml`, which run the
  figure checks; `mkdocs.yml` does not list the figure hook yet.

### 2. Charts

#### The house chart style

The maintainer's chart style is the `dataviz` agent skill (a Claude Code skill,
not a file in this repository). It sets the method: pick
the form first; validate the palette with its script; thin marks (bars at most
24 px thick with a 4 px rounded data end, 2 px lines, 8 px markers); a 2 px
surface gap between touching fills; recessive hairline grids; a legend for two
or more series plus sparing direct labels; one y-axis; a crosshair with one
tooltip listing every series on line charts and a per-mark tooltip on bars,
with "Same details on keyboard focus as on hover"; labels inserted with
`textContent`; a table view; a dark palette selected and validated for the dark
surface, switched by both `prefers-color-scheme` and the site's theme toggle;
a texture fill for colour-blind, print and forced-colour cases.

No library meets every row. Two rows no library meets on its own: keyboard focus
on marks, and the table view. Both have to come from the page around the chart,
which is also the accessible text alternative.

#### Candidates

Bundle sizes were measured on 2026-10-03: the published minified file from
jsDelivr, or an `esbuild --bundle --minify` of the named imports, then
`gzip -9`.

| | Vega-Lite + vega-embed | Apache ECharts | Observable Plot | Chart.js | uPlot |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Version, date | vega-lite 6.4.3 (2026-04-24), vega 6.4.0 (2026-08-14), vega-embed 7.3.0 (2026-09-23) | 6.1.0 (2026-05-19) | 0.6.17 (2025-02-14) | 4.5.1 (2025-10-13) | 1.6.32 (2025-03-14) |
| Licence | BSD-3-Clause | Apache-2.0 (licence and NOTICE travel with the copy) | ISC | MIT | MIT |
| Size, gzipped | 303 KB (vega + vega-lite + vega-embed bundled) | 368 KB full; 220 KB for bar, line, heatmap, tooltip, legend, aria, dataset and SVG renderer | 139 KB (Plot with the parts of d3 it uses) | 70 KB | 22 KB |
| Output | SVG or canvas | SVG or canvas | SVG | canvas | canvas |
| Authoring from a repository data file | JSON spec with `data.url`; validated by the published JSON schema | JSON-able option object; data inlined or loaded by script | JavaScript | JavaScript | JavaScript |
| Static render at build time | `vl-convert-python` 1.9.0.post1 (BSD-3, abi3 wheels for Linux, macOS, Windows): one spec rendered to SVG in 0.54 s on Python 3.14 here, with `aria-label` and graphics roles in the output | server-side SVG rendering (`ssr: true`) in Node | needs a DOM shim in Node | no (canvas) | no (canvas) |
| Light and dark | one config per mode; re-embed on toggle | `setTheme()` on the live chart | mark colours accept `var(--token)`, so the toggle needs no re-render (`isColor` in `options.js`) | re-render | re-render |
| Rounded 4 px data end, 24 px bars | `cornerRadiusEnd`, `size` | `borderRadius`, `barMaxWidth` | `rx1`/`ry2` options | `borderRadius`, `borderSkipped`, `maxBarThickness` | no |
| Crosshair, hover with exact values | nearest-point parameter with a rule; `vega-tooltip` escapes HTML by default | `axisPointer`; tooltip formatter may return an `HTMLElement` | `pointerX` with `tip` and rule marks | interaction mode `index`; vertical line needs a plugin | built in |
| Texture fill | no | `aria.decal` patterns | `url(#pattern)` fills | third-party plugin | no |
| Accessibility built in | `description` becomes `aria-label` on SVG output | `aria` generates a description from the data | `ariaLabel`, `ariaDescription` on the plot and marks | canvas: author adds `role="img"`, `aria-label` and fallback content | none |
| Keyboard focus on marks | no | no (not in `AriaOption`) | no | no | no |

Matplotlib is already in the Python harness locks (`python/requirements.txt`),
but it draws static images: no hover, and no theme switch without two renders.

#### Fit with the generator

All candidates load as page scripts, so the generator matters only for how a
chart gets onto a page. Material's instant navigation does not reload the page:
a chart script must subscribe to `document$` to run after each navigation
(Material's customisation guide).

The approach that survives a generator change is the one the repository already
uses for generated tables: a script under `scripts/docs/` renders each chart's
static SVG (light and dark) and its data table into committed files, and
`make docs-fragments-check` fails when they drift from the spec or the data. The
page shows the SVG and the table with plain Markdown and HTML; a small script
replaces the SVG with the interactive chart. Nothing depends on a MkDocs hook,
which Zensical does not support yet.

### 3. Diagrams

| | `tools/figures/` (interfig) | D2 | Mermaid (themed) | Hand-written SVG | Excalidraw |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Licence | MIT engine, vendored at a pinned commit; praetor-managed | MPL-2.0 (Go binary, v0.9.0 2026-09-07) | MIT | — | MIT |
| Source in the repository | TypeScript spec `docs/figures/<slug>.ts` | `.d2` text | text in the page | SVG markup | JSON scene |
| Output | committed static SVG, reduced-motion SVG, JSON; animated player with scenario tabs | SVG | rendered in the browser | SVG | SVG export |
| Light and dark | player uses the site's `--md-*` (or `--sl-*`) variables; the static SVG follows `prefers-color-scheme` | `--dark-theme` follows the system preference | Material applies the site palette to five diagram types | inline SVG can use the site's variables; an `<img>` cannot | export per theme |
| Checks | `make docs-figures` (`check`, `sources`), CI `Documentation Governance`; every `path:Symbol` evidence anchor must exist in the code | none in the repository | none beyond the build | none | none |
| Accessibility | `alt` of at most 125 characters, generated text description, static fallback, reduced motion honoured | `<title>` per shape | none by default | by hand | by hand |
| Limits | 40 boxes, 40 groups, 80 edges, 12 steps per figure | none practical | layout control is limited | effort per diagram | hand-drawn look only |
| Runtime cost | `loader.js` 3 KB on every page; `player.js` 75.6 KB gzipped, loaded near a figure | none | Material loads `https://unpkg.com/mermaid@11/dist/mermaid.min.js` at runtime: 976 KB gzipped for 11.17.2 | none | none |

praetor replaced its own Mermaid diagrams with this engine (praetor ADR-0015),
and its MkDocs and Starlight documentation presets both wire it in. The
repository already carries the engine and runs its checks; enabling it means
adding the hook to `mkdocs.yml` and writing specs.

### 4. Content that can use charts and diagrams now

#### Charts from data already in the repository

| Chart | Data | Form | Page |
| :--- | :--- | :--- | :--- |
| GPU twin exactness, 25 features by CUDA, SYCL and HIP | `scripts/ci/exact_twins.d/` (72 fragments) and `LIBM_TWINS` in `scripts/ci/cross_backend_calibration.py` (`ciede`, bounded at `1e-9` on all three) | status matrix with icon and label per cell; 72 exact, 3 bounded | `development/cross-backend-exact-twins.md`, `backends/index.md` |
| Upstream-parity allowlist by feature | `scripts/ci/upstream_parity.d/` (37 entries: `adm` 9, `float_adm` 5, `float_ms_ssim` 4, ...) | horizontal bars | `development/upstream-parity-allowlist.md` |
| Per-frame scores of the fork snapshots | `testdata/scores_cpu_{576,640,720,1080,4k}.json`, `testdata/scores_sycl_{a380,b580,uhd770}_*.json` (48 frames, every feature) | line chart with crosshair; CPU against a device as a difference chart | `development/cross-backend-gate.md` |
| Netflix benchmark fixtures | `testdata/netflix_benchmark_results.json` (three fixtures, `cpu` / `cuda` / `sycl`, pooled and per-frame VMAF; regenerated 2026-05-03) | per-frame line chart per fixture | `development/netflix-benchmark-baselines.md` |
| Tester hardware reports | `docs/hardware-reports/` (schema exists, no report yet) | counts by verdict and backend, once reports arrive | `hardware-reports/index.md` |

Throughput data exists too (`testdata/perf_benchmark_results.json`,
`testdata/perf_multi_resolution.json` with 50 cells from 2026-05-29). The release
plan in `AGENTS.md` §11 keeps benchmarks and performance claims out of the
candidates before RC7, so these charts wait for RC7 evidence.

#### Diagrams

- **Exists**: one Mermaid diagram, `architecture/phase4b-distributed-platform.md`.
  About 113 fenced text blocks in 46 files contain box-drawing or arrow art; most
  are in `rebase-notes.md`, ADRs and research digests, where they stay. The ones
  on reader-facing pages that should become figures are the tiny-AI pipeline in
  `ai/overview.md`, the operator and controller flows in
  `development/operator.md` and `server/controller.md`, and the ingestion flows
  under `ai/`. The repository tree in `architecture/index.md` is better left as
  text.
- **Missing**:
  - backend dispatch: CLI options, feature registry, the twin lookup through
    `provided_features` ([ADR-1359](../adr/1359-cli-feature-backend-twin.md)),
    then CPU, SIMD, CUDA, SYCL, HIP or Metal;
  - the test and golden-gate flow: unit tests, `make test-netflix-golden` on its
    isolated build ([ADR-1317](../adr/1317-golden-gate-build-isolation.md)), the
    cross-backend parity gate with tolerance 0 for exact twins;
  - the merge train and release flow: `development/merge-train.md`,
    release-please, the RC phases of `AGENTS.md` §11;
  - the tester-kit flow: tester image, report, schema check, report page
    (`usage/tester-image.md`, `scripts/ci/check-hardware-reports.py`).

### 5. Readability

#### Today's values, measured

A strict MkDocs build of `origin/master` was served locally and four pages
(`usage/cli/`, `backends/cuda/overview/`, `development/merge-train/`,
`adr/0403-mkdocs-strict-gate-validation-policy/`) were opened in headless Chrome
at 1280, 1440 and 1920 px. A script read the computed styles and counted the
characters on each rendered line of the first long paragraphs.

| Property | Value |
| :--- | :--- |
| Root font size | 20 px up to 1599 px wide, 22 px from 1600 px (`html { font-size: 125% }`, `137.5%` at `min-width: 100em`) |
| Body text | Inter, 16 px with a 25.6 px line (1.6); 17.6 px at 1920 px |
| Content column | 688 px wide at 1280 and 1440 px, 757 px at 1920 px (`.md-grid` is capped at 61 rem) |
| Characters per line | median 81 to 88 per page, longest lines 88 to 93 |
| Paragraph spacing | 16 px above and below (1 em) |
| Headings | h1 32 px, weight 300, in the lighter text colour; h2 25 px, weight 300; h3 20 px, weight 400 |
| Tables | 12.8 px (`.64rem`) |
| Inline code / code blocks | 13.6 px; code blocks with a 19 px line (1.4) |
| Admonitions | 12.8 px (`.64rem`, from the theme stylesheet) |
| Width of `1ch` in Inter at 16 px | 10.09 px, while the average prose character is 7.6 px |

The last row matters for the fix: a `max-width` in `ch` overshoots by a third
with Inter, because `ch` is the width of the digit zero (MDN). A measure has to
be set in `em` from the measured average: 66 characters × 7.6 px ≈ 503 px ≈
31.5 em.

Long paragraphs, counted in the Markdown sources (prose blocks of five words or
more, code fences removed): 21,550 paragraphs, median 33 words, 90th percentile
76 words; 151 paragraphs exceed 150 words and 28 exceed 300. Pages directly
under `docs/` (such as `state.md`) have the longest: 44 paragraphs above 150
words. These counts are input for the content work that splits paragraphs;
splitting is not part of this decision.

#### Guidance and targets

| Property | Guidance | Target for the redesign |
| :--- | :--- | :--- |
| Measure | Bringhurst: 45 to 75 characters, 66 ideal (webtypography.net 2.1.2); USWDS: 45 to 90, "measure 2" about 66 as the ideal target; Butterick: 45 to 90; WCAG 2.2 SC 1.4.8 (AAA): no more than 80 | 60 to 75 characters, about 66: `max-width` near 32 to 34 em on paragraphs, list items, block quotes and admonition text, with tables, code and figures free to use the column |
| Line height | WCAG 1.4.8: at least 1.5 within paragraphs; USWDS: at least 1.5 for long text | keep 1.6 for body text; 1.5 in code blocks |
| Paragraph spacing | USWDS: at least 1 em, more than 1.5 em disrupts the flow; Butterick: space around a heading larger than between paragraphs | keep 1 em between paragraphs; about 2 em above an h2 and 0.6 em below |
| Headings | Butterick: "space below to be smaller than the space above" | heavier weight (600 or more) in the full text colour instead of 300 in a light grey; a scale near 1.25 per level |
| Tables, code, admonitions | no external figure; current sizes are 80 to 85 % of body text | tables and admonitions at 14 to 15 px, code at 14 px; wide tables scroll inside their own container |
| Text spacing | WCAG 2.2 SC 1.4.12 (AA): no loss of content with line height 1.5, paragraph spacing 2 em, letter spacing 0.12 em, word spacing 0.16 em | the redesign passes a 1.4.12 override test |

#### How each generator exposes these settings

- **Material for MkDocs**: colours (`--md-primary-fg-color` and the scheme
  selectors) and fonts (`--md-text-font`) are CSS custom properties. Width and
  sizes are plain selectors (`.md-grid { max-width: 61rem }`,
  `.md-typeset { font-size: .8rem; line-height: 1.6 }`), overridden from an
  `extra_css` file.
- **Zensical**: the `classic` variant keeps Material's HTML and appearance, so
  the same `extra_css` applies; the `modern` variant has the same HTML with a
  different look.
- **Starlight**: width and type are tokens: `--sl-content-width: 45rem`,
  `--sl-text-body: 1rem`, `--sl-line-height: 1.75`, a heading scale from
  `--sl-text-h5` to `--sl-text-h1`, all in `props.css` and layered with cascade
  layers. 45 rem is 720 px at Starlight's 16 px root, about the width of
  today's column.
- **Docusaurus**: Infima CSS variables (`--ifm-*`) and component swizzling.

None of the generators sets a 66-character measure by default; each needs the
same override.

## Alternatives explored

- **Prototype Starlight on the full tree**: not done. It needs front matter for
  4,122 pages and a link transform before the first build, which is most of the
  migration. The decision does not depend on its build time, because the
  migration cost already rules it out for a styling goal; ADR-1508 records what
  would reopen it.
- **MkDocs 2.0**: excluded. It drops plugins and Material's theming, and offers
  no migration path.
- **ProperDocs**: a MkDocs 1.x fork under a new name. The Material team argues
  that forks which pretend to be `mkdocs` preserve the plugin ecosystem only on
  the surface; the project's last push was 2026-07-19.
- **Mermaid for every diagram**: works today with no new tooling, but loads
  976 KB of gzipped script from a third-party CDN on each diagram page and
  offers little layout control. The figure engine is already adopted and
  checked.
- **Matplotlib for charts**: already a dependency of the Python harness, but
  static only.

## Open questions

- Does Zensical 0.1.0 support `hooks` or another way to run the figure hook?
  If not, `node tools/figures/build.mjs portable` gives image fallbacks without
  the animated player.
- Can Zensical take a custom fence format function from a module in this
  repository? The chart design above avoids the question by generating outputs
  ahead of the build.
- Does Renovate keep the `<2` range for `mkdocs` in `docs/requirements.txt` when
  MkDocs 2.0 is released, or does it propose widening it? A package rule
  limiting `mkdocs` to `<2` would remove the doubt.
- Which navigation the redesign keeps per page was settled on 2026-10-03:
  the ADR navigation is collapsed behind the index and tag pages
  ([ADR-1510](../adr/1510-adr-nav-collapse-behind-index.md)). On
  `usage/cli/index.html` the primary navigation was 364 KB of the page's
  456 KB, and 1,839 of its 2,100 links pointed into `adr/` (628 to tag pages).

## Sources

Fetched or queried on 2026-10-03.

- Material for MkDocs blog, [What MkDocs 2.0 means for your documentation projects](https://squidfunk.github.io/mkdocs-material/blog/2026/02/18/mkdocs-2.0/) (2026-02-18, updated 2026-10-02).
- Material for MkDocs blog, [Zensical – A modern static site generator built by the creators of Material for MkDocs](https://squidfunk.github.io/mkdocs-material/blog/2025/11/05/zensical/) (2025-11-05).
- Zensical, [What changes on November 5, 2026](https://zensical.org/upcoming-changes).
- Zensical documentation, [MkDocs compatibility](https://zensical.org/compatibility/features/), and the sources at [zensical/docs](https://github.com/zensical/docs) commit `daf8b827` (2026-09-30): `docs/compatibility/mkdocs/migration.md`, `plugins.md`, `docs/setup/validation.md`, `docs/usage/build.md`, `docs/authoring/diagrams.md`.
- PyPI JSON API for `mkdocs`, `mkdocs-material`, `zensical`, `properdocs`, `pymdown-extensions`, `vl-convert-python`; npm registry for `@astrojs/starlight`, `astro`, `@docusaurus/core`, `echarts`, `vega`, `vega-lite`, `vega-embed`, `vega-tooltip`, `@observablehq/plot`, `chart.js`, `uplot`, `mermaid`, `@excalidraw/excalidraw`.
- [ProperDocs README](https://github.com/ProperDocs/properdocs).
- Starlight 0.42.5 package (`dist/schema.js`, `dist/loaders.js`, `dist/style/props.css`); [Starlight CSS and styling](https://starlight.astro.build/guides/css-and-tailwind/); [starlight-links-validator configuration](https://github.com/HiDeoo/starlight-links-validator) (`errorOnRelativeLinks`); [Pagefind](https://pagefind.app/).
- Docusaurus documentation sources in [facebook/docusaurus](https://github.com/facebook/docusaurus): `website/docs/guides/markdown-features/markdown-features-links.mdx`, `markdown-features-diagrams.mdx`, `website/docs/api/docusaurus.config.js.mdx`, `website/docs/styling-layout.mdx`.
- Material for MkDocs: [Customization](https://squidfunk.github.io/mkdocs-material/customization/), [Changing the colors](https://squidfunk.github.io/mkdocs-material/setup/changing-the-colors/), [Diagrams](https://squidfunk.github.io/mkdocs-material/reference/diagrams/), [Images](https://squidfunk.github.io/mkdocs-material/reference/images/); the installed 9.7.7 stylesheet and bundle.
- [ECharts accessibility handbook](https://echarts.apache.org/handbook/en/best-practices/aria/) and the 6.1.0 type definitions (`TooltipFormatterCallback`, `setTheme`, `AriaOption`); [Vega-Lite mark properties](https://vega.github.io/vega-lite/docs/mark.html); [vega-tooltip API](https://github.com/vega/vega-tooltip); Observable Plot sources and documentation in [observablehq/plot](https://github.com/observablehq/plot) (`docs/features/accessibility.md`, `docs/marks/rect.md`, `docs/interactions/pointer.md`, `src/options.js`); [Chart.js accessibility](https://www.chartjs.org/docs/latest/general/accessibility.html).
- D2 documentation source, `docs/tour/themes.md` in [terrastruct/d2-docs](https://github.com/terrastruct/d2-docs).
- praetor: `docs/adr/0015-interactive-figures-from-vendored-interfig.md`, `docs/presets/mkdocs/`, `docs/presets/starlight/` at commit `0af07a733`; this repository's `tools/figures/README.md`.
- W3C, [Understanding SC 1.4.8 Visual Presentation](https://www.w3.org/WAI/WCAG22/Understanding/visual-presentation.html) and [Understanding SC 1.4.12 Text Spacing](https://www.w3.org/WAI/WCAG22/Understanding/text-spacing.html).
- Matthew Butterick, Practical Typography: [Line length](https://practicaltypography.com/line-length.html), [Space above and below](https://practicaltypography.com/space-above-and-below.html).
- U.S. Web Design System, [Typography](https://designsystem.digital.gov/components/typography/) (updated 2024-03-20) and [Measure tokens](https://designsystem.digital.gov/design-tokens/typesetting/measure/) (updated 2024-05-15).
- Richard Rutter, [The Elements of Typographic Style Applied to the Web, 2.1.2](http://webtypography.net/2.1.2).
- MDN, [`<length>`: the `ch` unit](https://developer.mozilla.org/en-US/docs/Web/CSS/length).

## Reproducing the measurements

```bash
# MkDocs build time and size (docs lock in a fresh virtual environment)
python3 -m venv /tmp/docs-venv
/tmp/docs-venv/bin/pip install --require-hashes -r requirements/locks/package-build.txt
/tmp/docs-venv/bin/pip install --no-build-isolation --require-hashes -r docs/requirements-lock.txt
time /tmp/docs-venv/bin/mkdocs build --strict --site-dir /tmp/site-today
find /tmp/site-today -name '*.html' | wc -l
ls -l /tmp/site-today/search/search_index.json

# Zensical on a copy of the tree (it has no --site-dir option)
cp -r docs mkdocs.yml /tmp/zprobe/
python3 -m venv /tmp/zvenv && /tmp/zvenv/bin/pip install zensical==0.0.67
(cd /tmp/zprobe && time /tmp/zvenv/bin/zensical build --strict)

# Static chart render at build time
/tmp/zvenv/bin/pip install vl-convert-python==1.9.0.post1
/tmp/zvenv/bin/python -c "import vl_convert as v; print(v.get_vega_version(), v.get_vegalite_versions()[-1])"
```

## Related

- [ADR-1508](../adr/1508-docs-site-toolchain-and-charts.md): the decision this digest supports.
- [ADR-0403](../adr/0403-mkdocs-strict-gate-validation-policy.md), [ADR-0466](../adr/0466-mkdocs-strict-pre-push-hook.md), [ADR-0937](../adr/0937-mkdocs-nav-decade-buckets.md): the strict gate, the pre-push hook and the generated ADR navigation that any generator change must carry.
- [Pre-push mkdocs strict-mode gate](../development/pre-push-mkdocs-strict.md).
