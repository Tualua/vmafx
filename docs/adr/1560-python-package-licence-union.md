<!-- markdownlint-disable MD013 MD060 -->
# ADR-1560: A Python package declares the licences of every file its sdist and wheel carry, its compiled extension included

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, python, ci, fork-local

## Context

[ADR-1513](1513-production-artifact-licensing.md) decision 5 has each Python
package declare the union of the SPDX identifiers of its files and ship each
licence text through PEP 639 `license-files`. PR #1954 applied it to `vmaf-mcp`
and `vmaf-tune` with a test that read the `*.py` files under their `src/`
directories. A second, older test in `python/test/setup_metadata_test.py`
still required every package to declare `BSD-2-Clause-Patent`, so it failed on
`master` as soon as `vmaf-mcp` declared `EUPL-1.2`; it failed the hosted macOS
and ARM jobs of every pull request.

Checking the other five packages against their files showed that the old test
had also hidden wrong metadata: `vmaf-train` (`ai/`), `vmaf-dev-llm`,
`vmaf-roi-score`, the ensemble training kit and `vmaf` (the Python harness)
all declared `BSD-2-Clause-Patent`, while their files are EUPL-1.2 or a mix.
Fixing the test needs a definition of "the files a package ships" that can be
computed without building, and that holds for a package with a compiled
extension. The definition also reaches `vmaf-mcp`: hatchling copies the
nearest `.gitignore` into every sdist, which for every hatch package here is
the repository's, annotated `BSD-2-Clause-Patent` (Netflix 2020, Lusoris 2026)
in `REUSE.toml`; ADR-1513's `EUPL-1.2` for `vmaf-mcp` counted only its modules.

## Decision

One test, `test_every_python_package_declares_the_licences_of_the_files_it_ships`,
holds every package in `PACKAGE_PYPROJECTS` to the union of the licences of the
files it ships, read with the licence tool's own reader
(`tools/rc1-tester/image/licensing.py`: the file's SPDX header, else
`REUSE.toml`). What a package ships is read from its build configuration:

- **hatchling**: the sdist holds every tracked file of the project directory
  and the nearest `.gitignore` above it (hatchling's default sdist selection),
  and the wheel adds its force-included paths. A package that configures its own sdist selection fails the test until
  the selection is modelled.
- **setuptools** (`vmaf`): the wheel holds the modules of the listed packages,
  their package data, and the extension built from every `.pyx` in them plus
  the sources `setup.py` appends. The extension carries the code of each
  repository file it includes, so the test follows `#include` and Cython
  `cdef extern from` lines through the extension's include path without
  evaluating conditionals.

Untracked files are not counted (a clean checkout does not ship them); a
shipped file with no stated licence fails; the texts named by `license-files`
are not themselves counted. Each package ships `LICENSES/<id>.txt` for exactly
the identifiers it declares, byte for byte the repository copy. Negative
tests plant a contradicting declaration, an unlicensed file and a stale text.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Count only the wheel | Smaller unions (every hatch package but `vmaf-tune` would be EUPL-1.2 only) | PEP 639 metadata describes the sdist too; `vmaf-mcp` publishes both | The sdist ships files under other terms (`ai/data/feature_extractor.py`, `vmaf-roi-score`'s `pyproject.toml`, the `.gitignore`) |
| Build the sdist and wheel in the test | Exact file lists | Needs the build backends and a C toolchain in every test lane; minutes per run | The modelled lists match a real build: the `vmaf-mcp` sdist built with hatchling 1.32 holds exactly the tracked files, the repository `.gitignore` and `PKG-INFO` |
| Ask the C compiler for the extension's dependencies (`cc -M`) | Evaluates conditionals | Needs a compiler and the configured include path at test time | The regex closure finds the same files plus five architecture headers behind `#if`, all BSD-2-Clause-Patent; overstating can only add identifiers |
| Leave the `vmaf` harness at `BSD-2-Clause-Patent` | Netflix's package metadata unchanged | Its modules include `BSD-2-Clause` and `BSD-3-Clause-Clear` files and its compiled ADM extension includes three EUPL-1.2 headers (`adm_score.h`, `nonfinite_score.h`, `python/compat/config.h`) | Understates what the wheel carries, the same defect ADR-1513 fixed for `vmaf-mcp` |
| Leave the `.gitignore` out of the union, so `vmaf-mcp` stays `EUPL-1.2` | ADR-1513's expression kept | The file is in the archive under BSD-2-Clause-Patent and the sdist would carry no text for it | Understates the sdist |
| Stop hatchling copying it (`ignore-vcs = true` on the sdist target) | `EUPL-1.2` stays exact | Also drops the VCS exclusion, so the caches of a dirty tree enter the sdist; changes what the release workflow publishes | Declaring the licence is the smaller change |
| Keep the two-package test in `test_licensing_production.py` next to the new one | No edit to the tools suite | Two implementations of one check (HISS-19) | Folded into the one test |

## Consequences

- **Positive**: every package's `License-Expression` says what its files say,
  each package ships its texts, and a new file under other terms fails the
  test of its package instead of reaching a wheel.
- **Negative**: the `vmaf` harness now declares
  `BSD-2-Clause-Patent AND BSD-2-Clause AND BSD-3-Clause-Clear AND EUPL-1.2`,
  which follows from ADR-1250's combined-work consequence; every hatch package,
  `vmaf-mcp` included, declares `EUPL-1.2 AND BSD-2-Clause-Patent` and ships both
  texts; a header behind an `#if` the extension never takes still counts.
- **Neutral / follow-ups**: changing a `pyproject.toml` stales the fingerprint
  of every lock built from it. A licence field does not enter resolution, so
  the nine locks were restamped on their existing pins; a fresh resolution
  would have pulled newer releases that belong in a dependency update.

## References

- [ADR-1250](1250-eupl-fork-relicense.md), [ADR-1513](1513-production-artifact-licensing.md), [ADR-1503](1503-tester-artifact-licensing.md).
- Source: coordinator brief 2026-10-04 (paraphrased): fix the test so that it checks each package's declared expression against the SPDX headers of the files the package ships, fails on a package whose metadata disagrees with its files, and passes on `master`'s packages.
- PEP 639 (`License-Expression`, `License-File`).
