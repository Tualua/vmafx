<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1528: Every test file belongs to a suite that a required check runs

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: Lusoris
- **Tags**: ci, testing, python, rust, fork-local

## Context

On 2026-10-04 the maintainer noticed that no workflow ran `tools/vmaf-tune/tests`: 96 files and 2051 tests. A clean-environment audit of every test suite of the repository found more of the same:

- the `dev-llm` and `tools/vmaf-roi-score` suites, nine of the ten `compat/python-vmaf/tests` files, and about 60 Python and shell tests under `scripts/`, `dev/scripts/`, `testdata/` and two tool directories ran in no job;
- `python/test/test_adr0620_scaffold_audit_p0.py` did not match `python/tox.ini`'s `python_files = *test.py`, so neither tox nor the Coverage Gate ever collected it;
- `rust-ci.yml` ran `cargo test -p vmafx-sys`, which tests neither the safe `vmafx` crate nor `vmafx-tad`;
- some suites ran only partly: MCP Smoke skipped 19 tests (the `eval` extra and the golden YUVs were missing; #1976 added the extra while this change was in review), Tiny AI skipped 2 (no binary at the path they probe, no fixtures, no ffmpeg), and `dev-llm` skipped 7 (undeclared `onnx` / `pandas`).

Three suites failed when they were finally run, and the cause was code that had regressed with nothing noticing: `scripts/dev/hw_encoder_corpus.py` exited 0 on failure (fixed by #1982), and `scripts/ci/assertion-density.sh` passed when its source listing failed (fixed by #1984). Four contract tests that *did* run in CI were red on master because every hosted master run of the day was cancelled (fixed by #1977).

The rebase invariant for `noxfile.py` said that each package keeps its own recipe in `tests-and-quality-gates.yml`. Nothing checked that claim, so a new package directory could land with tests that never ran. The PR checklist was supposed to catch a missing CI lane, but it never did.

## Decision

We keep one job per suite, each with a venv built from that suite's hash lock. CI still does not call nox (ADR-0914 stands). `.github/test-suites.json` is the single list of suites: it maps each test-file pattern and directory to one suite and each suite to the required checks that run it. `scripts/ci/suite_registry.py check` runs in the new required `Tooling Tests` job and as a pre-commit hook. It fails when a tracked test file belongs to no suite or to two, when a suite path or `not_tests` entry matches no file, or when a suite names a check that the Required Checks Aggregator does not require. `suite_registry.py run tooling` runs the scripts-level suite from the same list, so the job cannot drift from the registry. The four package suites that ran nowhere become legs of a required `Python Package Tests` matrix, and Rust runs `cargo test --workspace`. The suites that skipped tests get the dependencies, fixtures and binaries those tests need; every pytest call in these jobs uses `-rs`, so any remaining skip prints its reason.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Registry file + check + per-suite jobs (chosen) | A new test directory fails a required check until it is wired; one place lists every suite and its job; the tooling job reads its file list from the same registry | One more data file to keep current; the check sees files, not individual test functions | — |
| Call `nox -s all` from one CI job | One command; the sessions already exist | Serialises every suite in one job (torch, onnx, optuna), so one red suite hides the others; contradicts ADR-0914; nox resolves interpreters itself, outside the pinned `PYTHON_CI_VERSION`; still nothing catches a suite missing from the noxfile | Loses per-suite status and adds a second interpreter source |
| Only add the missing jobs, no registry | Smallest change | The next new test directory goes unrun the same way; the claim in the rebase invariant stays unchecked | It treats the symptom: this audit found 13 unrun directories, not one |
| One pytest job over the whole tree from the root `pyproject.toml` | No per-package wiring | Packages have conflicting dependency sets (torch vs optuna vs mcp) and their own pytest configs; shell, Go and Rust tests are not pytest | It cannot run the suites as they are declared |

## Consequences

- **Positive**: a pull request that adds a test file outside every suite, or wires a suite to a check that blocks nothing, is red. Running every test as declared found five broken checks and one code regression that nothing had reported. MCP Smoke and Tiny AI now run every test except a sub-UID socket test that needs root or user namespaces.
- **Negative**: `Tooling Tests` runs on every pull request, takes about five minutes and duplicates some tests that other jobs also run in a narrower environment (pre-commit hooks, Rules steps). The tooling lock includes the docs stack and semgrep, so its install takes longer.
- **Neutral / follow-ups**: `vmaf-tune` still skips 15 tests in CI (a built `vmaf`, ffmpeg x265/QSV, the BBB corpus, the `train` extra), and `testdata/test_sycl_4k_repeat_determinism.py` skips without the local 4K fixtures. These skips are listed with their reasons in [docs/development/test-suites.md](../development/test-suites.md). The Coverage Gate's `--ignore` of `python/test/cy_test.py` and `cambi_test.py` has no recorded reason; tox runs both on C-core changes.

## References

- `req`: maintainer, 2026-10-04: "just not testing?" (on `tools/vmaf-tune/tests` running in no workflow).
- [ADR-0914](0914-unified-python-test-orchestrator.md) (nox stays a local affordance), [ADR-1140](1140-ci-impact-planner.md) (impact routing), [ADR-0313](0313-ci-required-checks-aggregator.md) / [ADR-1297](1297-ci-gate-every-reporting-check.md) (required checks), [ADR-1305](1305-hash-locked-python-installs.md) (hash-locked installs).
- [docs/development/test-suites.md](../development/test-suites.md).
