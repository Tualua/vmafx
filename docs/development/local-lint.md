# Local lint build profile and receipts

`make lint` runs the same native, Python, shell, Markdown and Go analysers as
CI against a build you configured yourself. Configure the profile first, run
`make lint`, and read the receipt directory when a run fails. This page covers
that workflow, the cppcheck model files and the receipts; the rest of the local
gate is in the [CI overview](ci.md#before-you-push).

## What `make lint` runs

`make lint` covers:

- clang-tidy and cppcheck for the configured tracked native sources;
- Ruff and Black for `python/`, `ai/` and `scripts/`;
- shell checks, Markdown checks, gosec and generated-document consistency
  checks.

Mypy remains advisory. IWYU and Semgrep are separate tools and jobs; this Make
target does not invoke them. Markdown defaults to changed files against
`origin/master`; use `MDLINT_SCOPE=all` for the configured whole-document scope.

This local source lint does not replace required CI, the
[Tidy Ratchet](tidy-ratchet.md), Netflix golden tests, sanitizer tests or
backend runtime and parity checks.

## Configure the profile first

`lint-c` asks Meson to reconfigure the existing build with no option overrides,
then runs the existing `build` target so generated headers and sources exist.
For a CPU profile without optional backends:

```bash
meson setup core/build-cpu core --buildtype=release \
  -Denable_cuda=false -Denable_sycl=false -Denable_hip=false \
  -Denable_metal=disabled -Denable_dnn=disabled -Denable_mcp=false
make lint BUILD_DIR=core/build-cpu LINT_JOBS=4
```

`LINT_JOBS` limits concurrent clang-tidy source jobs (default four).
`LINT_CONFIGURED_ARGS` passes helper options, such as repeated
`--clang-tidy-arg=--extra-arg=...` when the configured backend needs explicit
clang frontend arguments, or `--clang-tidy=/path/to/analyzer` and
`--cppcheck=/path/to/analyzer` for explicit tool paths. Backend toolchains must
exist; the helper does not install SDKs or convert unavailable backend checks
into passes.

### What the driver selects

The configured lint driver selects every tracked native source with a
configured command: top-level engine files, C++ CLI tools, tests and tracked
vendored sources. It keeps all command variants for a source, including
different test defines and include paths.

- Unconfigured backends are listed as outside the profile. A CPU result does
  not validate CUDA, SYCL, HIP, ARM or Metal sources absent from that database.
- Untracked and generated sources are recorded as excluded; the separate
  whole-tree [ratchet](tidy-ratchet.md) retains its generated-source and lane
  policies.

### The compile database

`make lint-c` explicitly exports `compile_commands.json` after Meson
regenerates the Ninja manifest and builds generated prerequisites, because
Meson 1.12 no longer materialises the database itself. The exporter requests
only Ninja's `c_COMPILER` and `cpp_COMPILER` rules, validates every entry, and
atomically replaces the last valid database. Missing rules, invalid JSON, an
empty result or a failed Ninja command stops the gate without destroying the
previous file.

!!! warning "Never substitute unfiltered `ninja -t compdb`"
    It includes link, custom and phony entries which are not native compile
    commands.

The root Makefile resolves its project-venv executables to absolute paths
(`$(VIRTUAL_ENV_PATH)`)
before calling Meson, and the Make entrypoints prepend the project virtual
environment to `PATH` as an absolute path. This is load-bearing: Meson records
the Ninja path it resolves and later invokes it from inside `core/build` while
generating the database. A relative `$(VENV)/bin` recipe prefix makes the
native build succeed but leaves the lint database absent (an invalid
`core/build/.venv/bin/ninja` lookup), so the single-source contract test
rejects that spelling. Keep the absolute-path assertion in
`test_lint_configured.py` when changing the build recipes.

## Receipts

Each run prints a private `BUILD_DIR/lint-configured-*/` receipt directory:

| File | Holds |
| --- | --- |
| `scope.json` | The input database hash, selected sources, command count, excluded scope and LTO adaptations. |
| `compile_commands.json` | The analyser copy of the database. |
| per-source clang-tidy logs | One log per analysed source. |
| `cppcheck.log` | The cppcheck output. |
| `result.json` | The retained results. |

The helper leaves Meson's resulting native database and build options
unchanged. Positive numeric GCC `-flto=N` becomes clang-compatible `-flto` only
in the analyser copy; other spellings, including invalid options, remain
visible to the analyser. Receipts are disposable build output and can be
archived before normal build cleanup.

### Failure semantics

- Every clang-tidy invocation includes `--warnings-as-errors=*`; without it
  clang-tidy prints ordinary diagnostics but normally exits zero. Any
  configured-source diagnostic therefore fails `lint-c`, regardless of whether
  the source originated in Netflix, a vendor, or the fork.
- Missing source files, missing, invalid or empty databases and missing tools
  also fail the gate.
- Cppcheck still runs after clang-tidy reports source diagnostics, so one
  analyser cannot hide the other's report.

Regression coverage runs locally and in the required Pre-Commit job:

```bash
python3 -m unittest discover -s scripts/ci/tests -p test_lint_configured.py
```

## Cppcheck configuration

Local and CI cppcheck share three pieces of configuration.

### POSIX model

Both load the official `posix` library model shipped with the installed tool.
It describes the pthread types and functions used by the fork, including the
Windows pthread compatibility surface; it does not select a Unix target or
replace compile-database platform defines. Without this model, cppcheck can
mistake an opaque `pthread_mutex_t` member for a C++ object that initializes
itself and incorrectly demand constructors for the surrounding C aggregate.
Keep the shipped model installed with the cppcheck binary. A missing model is
an error, not an ignored diagnostic.

The Cppcheck job also runs actual-tool controls against the repository's shared
C headers. Valid zero-initialized C and C++ uses must pass; an uninitialized
member read and a broken C++ constructor must still fail. Run those controls
locally with an installed cppcheck (`CPPCHECK_BIN` selects an explicit binary):

```bash
python3 -m unittest discover -s scripts/ci/tests -p test_cppcheck_posix_model.py
```

This configuration adds type and function knowledge without disabling any
diagnostic category. Local `--enable=all` and the CI job's existing
`warning,performance,portability` selection remain unchanged.

### Public entrypoints

Both also load
[`cppcheck-public-entrypoints.cfg`](../../scripts/ci/cppcheck-public-entrypoints.cfg)
([ADR-1246](../adr/1246-cppcheck-public-entrypoints.md)). It identifies 16
reviewed public C functions whose external callers are absent from the CPU
database, including disabled HIP and Metal fallbacks. It does not mark private
helpers or every backend scaffold as public. It does not disable body checks:
unlisted unused functions and defects inside listed functions still fail their
applicable checks. Missing or invalid model files fail analysis.

Before adding a name, verify its `VMAF_EXPORT` declaration and the header's
unconditional or conditional installation in
`core/include/libvmaf/meson.build`. The existing configured-driver tests
enforce those declarations and reject empty, duplicate, misspelled and
non-public entries. The real-tool suite above checks the external-root
behaviour, private-function and body-defect negatives and malformed models.

!!! note
    Cppcheck compares names without linkage or scope: a same-named static
    function is also treated as an entrypoint. Keep public C names unique; this
    model is not a visibility or ABI checker. See
    [the verified roots and version limits](../research/1246-cppcheck-public-entrypoints.md).

### Exhaustive analysis

Both paths use `--check-level=exhaustive`
([ADR-1245](../adr/1245-cppcheck-exhaustive-configured-analysis.md)). This
removes cppcheck's normal forward-branch budget instead of suppressing its
coverage notice. It can take substantially longer and can expose additional
real findings. The existing CI timeout and diagnostic selections remain in
force: timeout, memory exhaustion or any analyser failure is a failed run. The
real-tool suite above also checks a small branch-heavy function against normal
and exhaustive analysis; actual uninitialized reads must still fail. See the
[measured profile and tool-version limits](../research/1245-cppcheck-exhaustive-configured-analysis.md).
