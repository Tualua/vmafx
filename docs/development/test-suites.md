<!-- markdownlint-disable MD013 MD060 -->
# Test suites and the checks that run them

Every test file in the repository belongs to exactly one **suite**, and every
suite is run by one or more required CI checks.
[`.github/test-suites.json`](../../.github/test-suites.json) records both
mappings, and the required `Tooling Tests` job fails when a test file is not
in any suite ([ADR-1528](../adr/1528-test-suite-registry.md)).

## Adding a test

- **To an existing suite** (a new `test_*.py` under `tools/vmaf-tune/tests/`,
  a new `test-*.sh` under `scripts/ci/tests/`, ...): there is nothing to
  wire. The job that runs the suite picks the file up.
- **In a new directory**: add the directory to a suite's `paths` in
  `.github/test-suites.json`, or add a new suite there with the job that runs
  it. A new job must be in the `required` list of
  [`required-aggregator.yml`](../../.github/workflows/required-aggregator.yml),
  or the check refuses the suite. For a new Python package, follow
  [the Python test orchestrator page](python-test-orchestrator.md#adding-a-new-python-package)
  as well.
- **A file whose name looks like a test but is not one** (a driver script, a
  dataset module) goes under `not_tests`, with a reason.

Check the registry locally. The pre-commit hook `suite-registry` runs the same
command:

```bash
python3 scripts/ci/suite_registry.py check
python3 scripts/ci/suite_registry.py list tooling   # the files of one suite
```

A test file is any tracked file named `test_*.py`, `*_test.py`, `test_*.sh`,
`test-*.sh`, `*_test.sh`, `*_test.go`, `*_test.rs`, `test_*.c`, `test_*.cpp` or
`test_*.cu`, or any `.rs` file in a `tests/` directory. The check also fails
when a suite path or `not_tests` entry no longer matches any file, so the
registry cannot go stale.

## Suites

| Suite | Paths | Required check(s) | How it runs | Skipped in CI, and why |
|---|---|---|---|---|
| `core` | `core/test/`, `core/tools/test/` | `Ubuntu gcc`, `Linux Intel LLVM` | Meson tests (`scripts/ci/run_meson_test.py`); the backend-gated contract tests run in the all-backend `Linux Intel LLVM` leg | Device tests skip without a GPU |
| `python-harness` | `python/test/` | `Coverage Gate`, `Ubuntu gcc` | `pytest python/test/` against the gcov build, after an editable install of `python/` that compiles the Cython extension `cy_test.py` needs (as tox does); `tox -c python` on C-core changes | — |
| `compat` | `compat/python-vmaf/tests/` | `Python Package Tests (compat)` | `pytest compat/vmaf/tests` with `python/requirements-test-lock.txt`; the decorator file also runs on every OS in `build.yml` | — |
| `ai` | `ai/tests/`, `ai/sidecar/tests/` | `Tiny AI` | `pytest` with `ai/requirements-dev-lock.txt`, the job's DNN build as `VMAF_BIN`, the golden YUVs and ffmpeg | One socket test needs root or user namespaces to start a peer with another UID |
| `mcp` | `mcp-server/vmaf-mcp/tests/` | `MCP Smoke` | `pytest` with the dev lock (which carries the `eval` extra), the MCP build as `VMAF_BIN` and the golden YUVs | — |
| `rc1-tester` | `tools/rc1-tester/tests/` | `RC1 Tester Report` | `pytest` with the package's dev lock | — |
| `vmaf-tune` | `tools/vmaf-tune/tests/` | `Python Package Tests (vmaf-tune)` (its own job: it needs `MCP Smoke`) | `pytest` with the package's dev lock, MCP Smoke's `vmaf` (artifact `vmaf-cli-mcp`) as `VMAF_BIN_FOR_TESTS` and the golden YUVs; a skip for a missing binary or missing YUVs fails the job | 5: `VMAF_TUNE_INTEGRATION=1` with ffmpeg/x265 (2; opt-in, and the two-pass case fails today: `T-VMAF-TUNE-X265-TWO-PASS-CRF-2026-10-04`), QSV hardware (1), the BBB corpus (1), the `train` extra (1) |
| `dev-llm` | `dev-llm/tests/` | `Python Package Tests (dev-llm)` | `pytest` with the dev lock, which carries the `modelcard` extra | — |
| `vmaf-roi-score` | `tools/vmaf-roi-score/tests/` | `Python Package Tests (vmaf-roi-score)` | `pytest` with the package's dev lock | — |
| `go` | `api/`, `cmd/`, `internal/`, `pkg/` | `go vet + go test` | `go test ./...` against the CPU + ONNX Runtime libvmaf build | Individual tests skip when a tool they drive is absent |
| `rust` | `bindings/rust/` | `vmafx-sys CI` | `cargo test --workspace --all-features`, which also runs the inline tests of `core/src/feature/rust/tad` | — |
| `tooling` | `scripts/`, `dev/scripts/`, `ffmpeg-patches/test/`, `testdata/`, `tools/ensemble-training-kit/tests/`, `tools/external-bench/tests/` | `Tooling Tests` | `suite_registry.py run tooling`: one pytest run over the Python files, then each shell file with `bash`, using `requirements/locks/tooling-tests.txt` (pytest, PyYAML, reuse, semgrep, pre-commit and the docs stack) | Three live-build cases of `test_device_target_header_dependencies.py` (the `core` suite runs them under Meson with a build); two 4K cases of `testdata/test_sycl_4k_repeat_determinism.py` (the 4K fixtures are local-only) |

Each pytest call in these jobs passes `-rs`, so the job log names every
skipped test and its reason.

## Not tests

| Path | Why |
|---|---|
| `python/test/resource/` | Dataset definition modules named after their dataset |
| `scripts/ci/run_meson_test.py` | The Meson test runner wrapper ([ADR-1333](../adr/1333-meson-test-secret-env-sanitization.md)) |
| `scripts/test-matrix.sh` | Local driver that runs `make ci` in every `docker/dev` image |
| `testdata/test_all_backends.sh` | Benchmark driver that needs oneAPI, an installed `vmaf` and GPU devices; it asserts nothing |

## Running a suite locally

| Suite | Command |
|---|---|
| `tooling` | `nox -s tooling`, or install `requirements/locks/package-build.txt` and then `requirements/locks/tooling-tests.txt` (`--no-build-isolation`) and run `python scripts/ci/suite_registry.py run tooling` |
| `vmaf_tune`, `dev_llm`, `roi_score`, `mcp`, `ai`, `rc1_tester` | `nox -s <session>` ([orchestrator](python-test-orchestrator.md)) |
| `compat` | In a Python 3.14 venv: install `requirements/locks/package-build.txt`, then `python/requirements-test-lock.txt` (`--no-build-isolation`), and run `pytest compat/vmaf/tests`. The harness lock is Linux-only, so there is no nox session; `nox -s compat_decorator` runs the decorator file alone |
| `rust` | `LIBVMAF_PREFIX=<prefix> LD_LIBRARY_PATH=<prefix>/lib cargo test --workspace --all-features` after installing a CPU libvmaf ([Rust guide](rust.md)) |
| `core` | `python3 scripts/ci/run_meson_test.py -- -C build` |
| `go` | `go test ./...` ([CI overview](ci.md#go-checks)) |

The tooling suite's shell tests expect `git`, `bash`, `cc`, `readelf`,
`patchelf`, `node` and `timeout` on `PATH`, as the hosted Ubuntu runner has.
Docker is optional: the container-source test runs its Docker-backed cases
only when `docker info` succeeds.
