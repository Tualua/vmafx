<!-- markdownlint-disable MD013 MD060 -->
# Research-2143: Root licence files, package licence fields, and where LICENSE-MIT was published

- **Status**: Active
- **Workstream**: [ADR-1699](../adr/1699-root-licence-files-eupl.md), [ADR-1250](../adr/1250-eupl-fork-relicense.md)
- **Last updated**: 2026-10-05

## Question

What do licence detectors read from the repository root before and after the
layout of ADR-1699, which package manifests misstate the licences of the files
they ship, and which published artifacts contained the root `LICENSE-MIT` that
[ADR-0686](../adr/0686-vmafx-rebrand-aggressive-modernization.md) added?

This digest records facts and where they were read. It is not legal advice.

## Sources

- licensee 10.1.0, the detector GitHub's licence feature is built on
  (`gem install licensee` in the `ruby:3.4` image, `licensee detect <dir>`),
  run 2026-10-05 on copies of the root files.
- GitHub REST API `GET /repos/VMAFx/vmafx` and `GET /repos/VMAFx/vmafx/license`,
  read 2026-10-05.
- `cargo package --list -p <crate> --allow-dirty --offline` (cargo 1.98.1) and
  `cargo deny --offline check` (cargo-deny 0.19.8) in this worktree.
- Artifact Hub `docs/helm_annotations.md` (`artifacthub.io/license`), read
  2026-10-05.
- The registries: GHCR (package versions through the GitHub API; image
  manifests, configs and layers through `ghcr.io/v2`, anonymous pull tokens),
  `proxy.golang.org` (module list and the zips' central directories by HTTP
  range requests), PyPI's JSON API and files, the crates.io API and
  `cargo search vmafx`, and the release assets of every release (`gh release
  download`).
- `git log -S'LICENSE-MIT'`, `git cat-file -e <tag>:LICENSE-MIT` and
  `git ls-remote --tags origin` in this repository.

## Findings

### Root files and detectors

| Root files | licensee per file | licensee project result |
| --- | --- | --- |
| master `8e60965f0`: `LICENSE` (Netflix BSD-2-Clause-Patent text), `LICENSE-MIT` | `LICENSE`: `NOASSERTION` (closest: BSD-2-Clause-Patent, 76.64 %); `LICENSE-MIT`: `MIT`, exact match, attribution "Copyright (c) 2026 Lusoris" | `NOASSERTION` |
| First revision of this branch: `LICENSE` (`LICENSES/EUPL-1.2.txt`), `LICENSE-BSD-2-Clause-Patent` | `LICENSE`: `EUPL-1.2`, Dice 99.20 % (threshold 98 %); `LICENSE-BSD-2-Clause-Patent`: `NOASSERTION` (76.64 %) | `NOASSERTION` |
| ADR-1699: `LICENSE` (`LICENSES/EUPL-1.2.txt`), `NOTICE` (Netflix's text) | `LICENSE`: `EUPL-1.2`, 99.20 %; `NOTICE` is not a candidate licence file (matched files: `LICENSE` only) | `EUPL-1.2` |
| ADR-1699 with `LICENSE-MIT` planted back | `LICENSE`: `EUPL-1.2`; `LICENSE-MIT`: `MIT`, exact | `NOASSERTION` |
| `LICENSES/EUPL-1.2.txt` alone as `LICENSE` | `EUPL-1.2`, 99.20 % | `EUPL-1.2` |

On the whole tree (root files, `LICENSES/` and `Cargo.toml`) the result depends on
the licensee version, because 9.19.0 (PR licensee/licensee#926, January 2026)
added `LICENSES/` to the files it reads:

| Tree | licensee 9.18.0 | licensee 10.1.0 | GitHub (2026-10-05) |
| --- | --- | --- | --- |
| master layout (`LICENSE` Netflix, `LICENSE-MIT`, `LICENSES/`, `Cargo.toml`) | `NOASSERTION` (matched `LICENSE`, `LICENSE-MIT`, `Cargo.toml`) | `NOASSERTION` | `other` |
| this branch's working tree (`LICENSE`, `NOTICE`, `LICENSES/`, `Cargo.toml`) | `EUPL-1.2` (matched `LICENSE`, `Cargo.toml`) | `NOASSERTION` (matched `LICENSE`, the 15 `LICENSES/` texts, `Cargo.toml`) | `eupl-1.2` (`GET /repos/VMAFx/vmafx/license?ref=fix/root-licence-files-eupl`; `other` while the branch had `LICENSE-BSD-2-Clause-Patent`) |
| `eza-community/eza` (`LICENSE.txt` EUPL-1.2, `LICENSES/` with CC-BY-4.0, EUPL-1.2 and MIT) | `EUPL-1.2` | `NOASSERTION` | `eupl-1.2` |

GitHub's answers for eza and for this branch match 9.18.0's behaviour. In both versions `NOTICE`
is not a candidate licence file.

licensee reports a project licence only when the root's licence files agree on
one licence; two licence files with different licences give `NOASSERTION`, which
GitHub's API shows as `other`. Which names count is
`Licensee::ProjectFiles::LicenseFile::FILENAME_REGEXES` of 10.1.0: `LICENSE`,
`LICENCE`, `UNLICENSE` with any extension or `-`/`_` suffix, `COPYING*`,
`<word>-LICENSE*`, `<word>-COPYING*`, `OFL*`, `COPYRIGHT*` and `PATENTS*`, each
with a score; `NOTICE` matches only the catch-all, score 0. On 2026-10-05 the API returned
`{"key": "other", "spdx_id": "NOASSERTION"}` for the repository with
`LICENSE` as its licence file. The repository description already read "fork
code EUPL-1.2, Netflix code BSD-2-Clause-Patent"; the topics name no licence.
The byte-identical copy of `LICENSES/EUPL-1.2.txt` is detected, so the
repository keeps one EUPL-1.2 text rather than a second, differently wrapped one.

`git log -S'LICENSE-MIT'` finds three commits: `7f3504af4` (#1546, ADR-0686,
2026-05-28) added it, `384d97d03` (the layout move of the same day) touched it,
and `02e7d834f` (#1196) listed it in `.github/ci-impact.json`. No packaging
configuration (`pyproject.toml`, `Cargo.toml`, a Dockerfile `COPY`, a bundle
builder, a release workflow) ever named it.

### Package manifests against their files

Read with `tools/rc1-tester/image/licensing.py` (SPDX header, else
`REUSE.toml`), over the files each package ships:

| Manifest | Declared on master | The files carry |
| --- | --- | --- |
| `Cargo.toml` `[workspace.package]`, inherited by `vmafx-sys` | `BSD-2-Clause-Patent` | `EUPL-1.2`: `cargo package --list` gives `AGENTS.md`, `Cargo.toml`, `README.md`, `build.rs`, `examples/score.rs`, `src/lib.rs`, `src/safe.rs`, `tests/integration_test.rs` and the workspace `Cargo.lock`; the inherited root readme is not shipped because the crate has its own `README.md` |
| `bindings/rust/vmafx/Cargo.toml` | `BSD-2-Clause-Patent` | `EUPL-1.2` (eight sources, `AGENTS.md`, `README.md`, `Cargo.toml`, `Cargo.lock`) |
| `core/src/feature/rust/tad/Cargo.toml` (`publish = false`, inherits) | `BSD-2-Clause-Patent` | `EUPL-1.2 AND BSD-2-Clause-Patent`: `build.rs`, `src/lib.rs` and `Cargo.lock` are EUPL-1.2; the root `README.md` (copied in as the inherited readme) and the crate's `Cargo.toml` (no header; `REUSE.toml` `core/**`, 2016-2020 Netflix) are BSD-2-Clause-Patent |
| `deploy/helm/vmafx/Chart.yaml` `artifacthub.io/license` | `BSD-2-Clause-Patent` | `EUPL-1.2` (the chart's 34 own files) |
| `deploy/helm/vmafx/charts/prometheus-pushgateway-3.9.0.tgz` | its own `Chart.yaml`: `Apache-2.0` | `REUSE.toml` recorded `EUPL-1.2` (the tree default); the upstream repository `prometheus-community/helm-charts` is Apache-2.0 and the archive holds no licence text |
| `tools/rc1-tester/pyproject.toml` | `EUPL-1.2` | `EUPL-1.2 AND MIT`: the sdist carries `image/licenses/hacl-star.txt` and `image/licenses/nv-codec-headers.txt`, MIT in `REUSE.toml` |
| The seven packages of ADR-1560 | as declared | already equal (held by `python/test/setup_metadata_test.py`) |
| `tools/markdownlint/package.json` | no `license` | `"private": true`; npm refuses to publish it |
| `pyproject.toml` (root) | no `license` | the repository's tool configuration, never built (ADR-1127) |

`go.mod` has no licence field. Artifact Hub requires `artifacthub.io/license`
to be "a valid SPDX identifier". With `EUPL-1.2` for `vmafx-sys` and `vmafx`,
cargo-deny needs `EUPL-1.2` in `deny.toml`'s allow list; `vmafx-tad` is private
and ignored, and `BSD-2-Clause-Patent` is then unmatched (cargo-deny warns about
an unused allowance), so the list swaps one for the other: `licenses ok`.

### Where LICENSE-MIT was published

| Artifact | Contained `LICENSE-MIT`? | How it was read |
| --- | --- | --- |
| `proxy.golang.org`, module `github.com/VMAFx/vmafx` | **Yes** for `v1.0.0-rc.1` and `v1.0.0-rc.2`: `github.com/VMAFx/vmafx@v1.0.0-rc.N/LICENSE-MIT`, 1,064 bytes, "MIT License / Copyright (c) 2026 Lusoris", next to `LICENSE` (Netflix's text). The proxy's list holds 26 versions; the other 24 are Netflix tags from 2016 to 2023, before the file existed | `@v/list`; central directory of each zip (92 MB) by range requests, about 0.9 MB read |
| GitHub source archives (`/archive/refs/tags/<tag>.zip`, `.tar.gz`) | **Yes** for every tag on `origin` whose tree has it: `v1.0.0-rc.1`, `v1.0.0-rc.2`, `tester-20261004-4d3792b3`, `tester-20261004-860050c3`, `tester-windows-20261004-2889f963`, `archive/eupl-relicense-v1`; and the `master` tree from 2026-05-28 to this change. GitHub builds these with `git archive`; `.gitattributes` has no `export-ignore` | `git cat-file -e <tag>:LICENSE-MIT`, `git ls-remote --tags origin` |
| GHCR `ghcr.io/vmafx/vmafx`, `vmafx-server`, `vmafx-operator` (39 tags: rc.1 and rc.2 CPU, server, CUDA 13, oneAPI 2025, their `-source` images, three tester builds with their GPU kits and sources) | No | Every tag's image config history: the final stages copy build outputs (`/dist/lib/`, `/dist/model/`, `/dist/bin/`, `/venv`, `/opt/vmafx/...`, named tester files), never the repository root. Every layer listed (amd64) for the CPU and CUDA 13 images and the `vmafx-server` and `vmafx-operator` images of both releases (140 layers); the CPU tester `v1.0.0-rc.2-411-g854bf047e-tester` except its venv and fixture layers; `vmafx-operator:v1.0.0-rc.1-source` holds Debian source packages and dependency module zips, not this module |
| Release assets of `v1.0.0-rc.1`, `v1.0.0-rc.2` (`libvmaf.so*`, `vmaf`, `models.tar.gz`, `licenses-*.tar.gz`, `THIRD_PARTY_NOTICES-*`) | No | The 14 licence tarballs extracted: no file equal to `LICENSE-MIT`, no "Copyright (c) 2026 Lusoris"; their 18 `texts/MIT.txt` are `LICENSES/MIT.txt` and their 20 `texts/BSD-2-Clause-Patent.txt` are the root `LICENSE` of the time, byte for byte |
| Tester bundles (macOS `tester-20261004-*`, Windows `tester-windows-20261004-2889f963`) | No | Their published `bundle-files*.txt` listings (5 files) |
| `vmaf-mcp` 1.0.0rc1, 1.0.0rc2 on PyPI (sdist and wheel; yanked per ADR-1578) | No | The four files downloaded: none holds a licence file of any name |
| crates.io | Nothing published: `vmafx`, `vmafx-sys`, `vmafx-tad` do not exist; `cargo search vmafx` returns nothing | crates.io API |
| Helm chart | Not published (`helm-chart.yml` lints and tests only) | workflow |

The root `LICENSE` of the time (Netflix's text) reached the artifacts as
`BSD-2-Clause-Patent.txt` through `licensing.json` `spdx_texts`; after ADR-1699
the same bytes come from `NOTICE`, so a rebuilt artifact's notices do not change.

### What the Go retraction reaches

The root `go.mod` now carries `retract [v1.0.0-rc.1, v1.0.0-rc.2]`. The Go
modules reference says the go command loads retractions from the `go.mod` of
the version `@latest` resolves to, and that the retracting version must be
higher than every other release or pre-release. For this module path the proxy
lists 26 versions (go 1.27.1, `go list -m -versions github.com/VMAFx/vmafx`,
2026-10-05): `v1.0.0-rc.1`, `v1.0.0-rc.2`, Netflix's tags `v1.0.2` to
`v1.5.3`, and `v2.0.0+incompatible` to `v3.0.0+incompatible`.
`go list -m github.com/VMAFx/vmafx@latest` resolves to `v3.0.0+incompatible`:
the highest compatible release, `v1.5.3`, has no `go.mod`, so the
`+incompatible` versions stay eligible.

A local file-based module proxy reproduced that resolution and showed the
consequence:

| Versions served | `@latest` | `go list -m -versions` |
| --- | --- | --- |
| rc.1, rc.2, `v1.5.3`, `v3.0.0+incompatible` (today) | `v3.0.0+incompatible` | rc.1, rc.2, `v1.5.3`, `v3.0.0+incompatible` |
| + `v1.0.0-rc.3` whose `go.mod` has the retraction | `v3.0.0+incompatible` | rc.1, rc.2 still listed; `-u -json` of rc.2 shows no `Retracted` |
| + `v1.0.0` with the retraction | `v3.0.0+incompatible` | unchanged |
| + `v1.6.0` with the retraction | `v1.6.0` | rc.1, rc.2 gone (listed only with `-retracted`) |
| only rc.1, rc.2, rc.3 (control) | rc.3 | rc.1, rc.2 gone; `-u -json` of rc.2 shows `Retracted` with the rationale |

So with Netflix's tags in the module's version list, a retraction published in
a `v1.0.0-*` or `v1.0.0` version is not read by the go command.

### `.toml` files and the provenance tool

`relicense_fork_files.py` skipped every file it had no comment style for, which
included `.toml`. With `.toml` classified (praetor's `.codex/agents/*.toml`
projections excluded), 15 files changed: 13 fork files gained the EUPL-1.2
header (`REUSE.toml`, `.gitleaks.toml`, `.helix/config.toml`,
`.helix/languages.toml`, `osv-scanner.toml`, the root `pyproject.toml`, the
`ai/`, `dev-llm/`, `mcp-server/vmaf-mcp/` and ensemble-kit `pyproject.toml`, and
the three crate manifests), and `tools/vmaf-tune/pyproject.toml` and
`tools/vmaf-roi-score/pyproject.toml` (both "Copyright 2026 Lusoris") moved from
`BSD-2-Clause-Patent` to `EUPL-1.2`. Those two had been kept by veto 3 because
upstream has a `python/pyproject.toml`; the name now carries no provenance
signal. `bindings/rust/vmafx-sys/Cargo.toml` was flagged by the derivation
detector for "bindings.rs is produced by bindgen from libvmaf.h"; it is a
manifest and was recorded under `[not_ports]`. `python/pyproject.toml` (an
upstream path) keeps its terms through `REUSE.toml`.

## Alternatives explored

- **Canonical choosealicense text instead of `LICENSES/EUPL-1.2.txt`.** Not
  needed: the SPDX text already passes licensee's threshold (99.20 %), and a
  second EUPL-1.2 text would differ from the one REUSE and every package ship.
- **Counting the Helm subchart in the chart's own licence.** Rejected in
  ADR-1699: Artifact Hub takes one identifier, and the subchart declares its
  own; the gate instead holds the subchart's `REUSE.toml` record to its
  declaration, which found the `EUPL-1.2` record above.
- **Running `cargo package --list` in the gate.** It needs a Rust toolchain and
  a populated registry cache; the gate models cargo's selection instead and
  refuses `include`, `exclude` and `license-file`. For all three crates the
  model's file set is `cargo package --list` (its generated `Cargo.toml` and
  `.cargo_vcs_info.json` mapped to their sources) plus the workspace manifest
  whose inherited fields cargo writes into the generated `Cargo.toml`.

## Open questions

- GitHub's licence API reports `eupl-1.2` for this branch's ref; the sidebar
  shows the default branch. A GitHub detector that reads `LICENSES/` (licensee
  9.19.0 or later) would report `NOASSERTION` for this tree.
- The `go.mod` retraction reaches Go clients only through a version that
  `@latest` resolves to; see "What the Go retraction reaches".
- The crates ship no licence text; none is published.

## Related

- ADRs: [ADR-1699](../adr/1699-root-licence-files-eupl.md),
  [ADR-1250](../adr/1250-eupl-fork-relicense.md),
  [ADR-0686](../adr/0686-vmafx-rebrand-aggressive-modernization.md),
  [ADR-1513](../adr/1513-production-artifact-licensing.md),
  [ADR-1560](../adr/1560-python-package-licence-union.md),
  [ADR-1578](../adr/1578-published-rc-licence-companions.md)
- Research: [Research-2140](2140-production-artifact-licence-audit.md)
- PRs: #1546 (added `LICENSE-MIT`)
