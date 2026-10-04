<!-- markdownlint-disable MD060 -->
# Python test orchestrator (nox)

The fork ships multiple Python distributions plus compatibility regression
suites with different setup needs. To avoid memorising each recipe, the repo
has a top-level [`noxfile.py`](../../noxfile.py) that exposes each suite as a
named session.

Nox is a **local-developer affordance**, not a CI gate. CI runs each suite in
its own job of `.github/workflows/tests-and-quality-gates.yml`: it installs the
suite's manifest-owned hash lock, installs the package itself with
`--no-deps --no-build-isolation`, and runs `pytest`. The compatibility decorator
suite also runs on every OS in [build.yml](../../.github/workflows/build.yml)
to exercise the native POSIX and Windows lock implementations. Which job runs
which suite is [the test-suite registry](test-suites.md)
([ADR-1528](../adr/1528-test-suite-registry.md)). The decision record for nox
is [ADR-0914](../adr/0914-unified-python-test-orchestrator.md).

## Install

```bash
python3 -m pip install --require-hashes -r requirements/locks/nox.txt
```

Nox creates its own per-session venvs by default
(`.nox/<session-name>/`) — you do not need to pre-create one. With
`nox.options.reuse_existing_virtualenvs = True` set in the noxfile,
re-runs reuse the environment while still reconciling its locked installs. Use
Nox's `-R` option only when you intentionally want to reuse the environment and
skip installation.

Every package session installs a manifest-owned hash lock first, then installs
the local package with `--no-deps --no-build-isolation`. Every session uses
Python 3.14, the interpreter CI pins; `ensemble_kit` runs a shell test and
creates no venv.

## Sessions

| Session | Target | Notes |
|---|---|---|
| `ai` | `ai/tests/`, `ai/sidecar/tests/` | Tiny-AI training scripts and online-training sidecar. Heavy: pulls `torch`, `lightning`. |
| `mcp` | `mcp-server/vmaf-mcp/tests/` | MCP JSON-RPC server. |
| `vmaf_tune` | `tools/vmaf-tune/tests/` | Encode-tuning harness. Ten tests drive a real `vmaf` on the golden pair: set `VMAF_BIN_FOR_TESTS` to a built CLI and run `scripts/test/fetch-test-yuvs.sh`, as the CI job does. |
| `dev_llm` | `dev-llm/tests/` | Local-LLM helper (Ollama-backed). |
| `roi_score` | `tools/vmaf-roi-score/tests/` | Saliency-aware ROI tooling. |
| `ensemble_kit` | `tools/ensemble-training-kit/tests/test_platform_detect.sh` | Platform and encoder detection of the ensemble training kit; a shell test, no venv. |
| `compat_decorator` | `compat/vmaf/tests/test_decorator_extended.py` | SHA-256 memoization, recursion, thread/spawn concurrency, and native file locking. |
| `tooling` | `scripts/`, `dev/scripts/`, `ffmpeg-patches/test/`, `testdata/`, two tool test directories | The `Tooling Tests` job's suite: the registry check, then `scripts/ci/suite_registry.py run tooling`. |
| `rc1_tester` | `tools/rc1-tester/tests/` | Dependency-free RC1 hardware/report collector. |
| `python_harness` | `python/tox.ini` | Delegates to legacy tox (Cython + golden-data). |
| `all` | every per-package suite and `tooling` | Excludes `python_harness` (needs C build). |
| `lint` | `python/`, `ai/`, `scripts/`, `tools/rc1-tester/` | Ruff + Black, check-only. |

## Usage

```bash
nox -l                          # list every session with its docstring
nox -s ai                       # run ai/tests/ and ai/sidecar/tests/ in an isolated venv
nox -s mcp vmaf_tune            # run multiple suites in sequence
nox -s compat_decorator         # run the compatibility decorator regressions
nox -s rc1_tester               # run RC1 collector regressions
nox -s python_harness           # invoke the legacy python/ tox harness
nox -s all                      # every fork-local Python package
nox -s lint                     # check-only Ruff + Black
nox -s ai -- -k test_smoke      # pass posargs through to pytest
```

The `--` separator forwards everything after it to the underlying
`pytest` invocation, so `-k`, `-x`, `--lf`, `--maxfail=N` and friends
work as usual.

## Adding a new Python package

When a new package lands under `ai/`, `mcp-server/`, `tools/`, or
similar, add **both**:

1. A new session in [`noxfile.py`](../../noxfile.py) following the
   existing one-per-package template, plus a dedicated development lock entry
   in [`manifest.json`](../../requirements/locks/manifest.json). Pin the session
   interpreter when the package's `requires-python` range excludes the Nox host.
2. A new leg of the `python-package-tests` matrix in
   [`tests-and-quality-gates.yml`](../../.github/workflows/tests-and-quality-gates.yml)
   (or a job of its own when the suite needs a build), which installs the same
   hash lock, installs the local package with `--no-deps --no-build-isolation`,
   and runs `pytest <path>/tests/ -rs`. CI does not call nox.
3. A suite in [`.github/test-suites.json`](../../.github/test-suites.json)
   naming that job's check, and the check in the `required` and
   `strictMustReport` lists of `required-aggregator.yml`.

`scripts/ci/suite_registry.py check` (the `Tooling Tests` job and the
`suite-registry` pre-commit hook) fails while a test file is in no suite or a
suite names a check that blocks nothing, so a missing CI lane is caught
automatically. Nothing catches a missing nox session; review does.

## Why nox and not tox

ADR-0914 § Alternatives considered. Briefly: tox's INI config does not
compose well across N packages with different `requires-python` ranges
and conflicting heavy deps (`torch` vs `optuna` vs `mcp`); a single
Makefile target loses the throw-away-venv isolation that the CI lanes
rely on; `pytest-xdist --rootdir` collapses dep trees that must stay
separate (torch + optuna co-resolve poorly).

## What nox does **not** do

- It does not call `meson` / `ninja` — the C build is out of scope.
  Use `make build` first if your suite needs a built `vmaf` binary.
- It does not run the Netflix CPU golden-data gate. That stays
  exclusively in `make test-netflix-golden` (which drives pytest
  directly against the legacy `python/test/` files).
- It does not replace `make lint`. The `lint` session is convenience;
  `make lint` remains the canonical CI invocation.
