<!-- markdownlint-disable MD013 MD060 -->
# VMAF Fork — Engineering Principles

This page defines the non-negotiable standards for code merged to `master`.
Read it before writing C.

Requirements are codified in one of `.clang-tidy`, `.cppcheck-suppressions.txt`,
`.semgrep.yml`, `.pre-commit-config.yaml` or
`.github/workflows/{lint-and-format,security-scans,supply-chain}.yml`.
A rule that is not yet codified in tooling is marked as a target on this
page and tracked as an open row in the repository's versioned
[`docs/state.md`](state.md) register.

**Scope (ADR-1142).** Every principle and every gate below applies to the
*whole* tree — upstream-mirror Netflix code, vendored libraries, fork-added
code, CUDA / HIP / SYCL / Metal kernels, SIMD paths, tests, tools and
scripts alike. There is no origin-, language- or backend-based tier. The
one invariant that outranks the standards is numerical correctness as pinned
by the Netflix golden assertions (section 8 of
[AGENTS.md](https://github.com/VMAFx/vmafx/blob/master/AGENTS.md), and
[section 3.1](#31-netflix-golden-data-gate) below). Whole-tree lint debt is
measured and may only decrease (`make tidy-ratchet`, see
[docs/development/ci.md](development/ci.md#whole-tree-lint-ratchet-adr-1142)).

## 1. Coding

### 1.1 NASA/JPL "Power of 10" rules (adapted for C)

1. **Simple control flow.** No `goto`, no `setjmp`/`longjmp`, no recursion
   (static or dynamic).
2. **Bounded loops.** Every loop has a statically-verifiable upper bound on
   iterations.
3. **No dynamic allocation in hot paths.** `malloc`/`calloc`/`realloc` are
   permitted at
   initialization only; frame-loop code paths are allocation-free.
4. **Short functions.** Function bodies ≤ 60 lines (a single printed page).
   Enforced by
   `readability-function-size` in clang-tidy with `LineThreshold: 60`.
5. **Assertion density.** ≥ 2 runtime assertions per function on average. Use
   `assert()`
   for invariants; in hot loops where `assert()` cost matters, use
   `VMAF_ASSERT_DEBUG()`
   which compiles away in release.
6. **Minimal scope.** Declare variables at the smallest possible scope.
7. **Check return values.** Every non-void function return must be checked and
   handled. Casting a result to `void` does not handle an error. Enforced by
   `cert-err33-c`.
8. **Preprocessor restraint.** Only header inclusion (`#include`) and simple
   macros. No
   token pasting, no recursive macros, no conditional compilation beyond
   platform/feature
   toggles established at build-system level.
9. **Pointer restraint.** Single level of dereferencing in most cases. No
   function
   pointers except where essential for dispatch (feature extractor registry,
   SIMD
   runtime selection).
10. **Max strictness.** Zero compiler warnings (HISS-10). `core/meson.build`
    builds at
    `warning_level=2` (`-Wall -Wextra`); it does not set `-Wpedantic` or
    `-Werror`. The
    checks listed under `WarningsAsErrors` in `.clang-tidy` fail the build.
    Static
    analyzers (clang-tidy, cppcheck, scan-build) must finish with zero findings
    at
    enabled levels.

### 1.2 JPL Institutional Coding Standard for C — applicable subset

The JPL-C-STD is the 31-rule superset of Power of 10. The following rules extend
1.1 and
are additionally enforced. The rule numbers are the JPL numbers.

#### Preprocessor and headers

- **Rule 11.** Place `#include` directives only at the top of a file.
- **Rule 12.** Do not place executable code inside a `#include`d file.
- **Rule 13.** Do not use reserved names (names beginning with `_` followed by
  uppercase
  letter, or `__`).
- **Rule 19.** All non-trivial types used in a function's public interface must
  be
  defined in a header.
- **Rule 31.** All headers must be self-contained (include what they need) and
  include-guard protected.

#### Casts, expressions and initialisation

- **Rule 14.** All type casts must be explicit.
- **Rule 15.** The evaluation order of operands must not matter. No `i++` or
  `++i` used
  within a larger expression; split into separate statements.
- **Rule 16.** No assignments inside expressions.
- **Rule 17.** All variables must be initialized before use.
- **Rule 22.** No side effects in conditional expressions.
- **Rule 20.** Use `const` aggressively. Anything not modified is `const`.

#### Globals and linkage

- **Rule 18.** All global variables must have a single declaration, and it must
  be in the
  file that owns them.
- **Rule 21.** Use `static` on every symbol not referenced outside its
  translation unit.

#### Switch and control flow

- **Rule 23.** All switch statements must have a default case.
- **Rule 24.** No fall-through in switch statements except where explicitly
  marked with
  `/* FALLTHROUGH */` or `__attribute__((fallthrough))`.
- **Rule 25.** Functions must have a single exit point where practical. Early
  returns are
  permitted for validation at function entry.
- **Rule 26.** Use of pointer arithmetic is restricted to array traversal within
  bounded
  loops.

#### Signals, variadics and library functions

- **Rule 27.** No use of `<setjmp.h>`, `<signal.h>` handlers that do real work,
  or any
  non-async-signal-safe function inside a signal handler.
- **Rule 28.** No variadic functions except standard `printf`/`scanf` family.
- **Rule 29.** No use of `<stdarg.h>` macros outside of printf/scanf wrappers.
- **Rule 30.** Banned functions: `gets`, `strcpy`, `strcat`, `sprintf`, `strtok`
  (non-reentrant), `atoi`, `atol`, `atof` (no error reporting), `rand`
  (non-cryptographic
  and non-reproducible), `system` (outside build scripts). Use: `fgets`,
  `strncpy` (with
  explicit null-termination) / `snprintf` / `strlcpy`, `snprintf`, `strtok_r`,
  `strtol`
  errno checking, `arc4random` or a seeded `xorshift`, direct process calls with
  arg arrays.

### 1.3 SEI CERT C & CERT C++

Full compliance required for:

- **INT (Integers)** — no signed overflow, always check `size_t` arithmetic,
  narrow casts
- **STR (Strings)** — no off-by-one, always check buffer bounds
- **MEM (Memory management)** — every `malloc` has a matching `free`; no
  double-free; no
  use-after-free (ASan enforced)
- **FIO (I/O)** — check every `fopen`, `fread`, `fwrite`, `fclose` return
- **EXP (Expressions)** — no undefined behavior; no sequence-point violations
- **CON (Concurrency)** — use atomics correctly (`_Atomic`); no data races (TSan
  nightly)
- **ENV (Environment)** — never trust `getenv` input without validation

Enforcement: `cert-*` checks in `.clang-tidy` all enabled. The noisy
`clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` subset
(Microsoft C11 `_s` functions) is explicitly disabled — it does not map to
any portable POSIX API.

### 1.4 MISRA C:2012 (informative subset)

Applied where it does not conflict with existing libvmaf conventions:

- **Rule 8.5** (one external declaration per identifier in a single file)
- **Rule 10.1–10.8** (essential type rules — stricter than standard C integer
  promotions)
- **Rule 17.7** (function return values must be used) — matches Power of 10 #7.
  MISRA
  allows an explicit `void` discard; the project rule does not (section 1.1,
  rule 7).
- **Rule 21.x** (banned functions) — matches JPL rule 30

Not enforced as PR-blocking; informational in review.

### 1.5 Style

- **C:** Existing libvmaf conventions preserved (K&R braces, 4-space indent,
  100-char
  columns). Codified in [.clang-format](../.clang-format).
- **C++ (SYCL code):** same as C where applicable; RAII encouraged for
  queue/context
  wrappers; no exceptions in hot paths.
- **Python:** PEP 8 + black (line-length 100) + ruff (import sorting via ruff's
  `I` ruleset — ADR-1126). Codified in
  [pyproject.toml](../pyproject.toml).
- **CUDA (`.cu`):** follows C style; kernel names `kernel_*`; device helpers
  `device_*`.
- **Shell:** shfmt + shellcheck; `#!/usr/bin/env bash`; `set -euo pipefail`.

### 1.6 Headers shared by C and C++

**Rule.** A type that both a C and a C++ translation unit read has one
definition, in
the C form. An enum in such a header does not get a fixed underlying type for
C++ only
(`#ifdef __cplusplus` / `enum E : uint8_t`).

**Why.** C cannot spell the fixed type on the required MSVC lane, so the enum
would be
`int`-sized in C and narrower in C++. A value passed by value, stored in a
shared struct
or read through a pointer would then be read with the wrong size
([ADR-1470](adr/1470-c-cxx-shared-enum-one-definition.md)).

**Instead.** Suppress `performance-enum-size` and `modernize-use-using` on the
definition
with a citation.

**Exception.** An enum whose size is part of an ABI may state `: unsigned int`
for C++
when it carries a `UINT_MAX` enumerator that holds the C enum to the same four
bytes
(`core/src/model.h`).

**Guard.** `core/test/test_c_cxx_enum_definition_contract.py` checks every
header a `.c`
file under `core/` includes.

## 2. Security

See [SECURITY.md](../SECURITY.md) for reporting policy.

### 2.1 Input validation at boundaries

- All CLI inputs validated at parse time (see `core/tools/cli_parse.cpp`).
- All public libvmaf API entry points validate pointer non-null + struct
  version.
- Video frame dimensions bounded; no negative or zero-size frames accepted.
- File paths validated for NULL and checked with `access()` before open.

### 2.2 Memory safety

Enforced today (`.github/workflows/sanitizers.yml`):

- **ASan + UBSan** on every PR (required contexts `Sanitizers (address)`,
  `Sanitizers (undefined)` and `Sanitizers ASan+UBSan`).
- **TSan** (`Sanitizers (thread)`) on pushes to `master`.
- **libFuzzer + ASan** nightly at 04:30 UTC.

Target hardening flags. These are the intended release-build settings; none of
them is
defined in `core/meson.build`, `core/src/meson.build` or the container build
files yet:

- `_FORTIFY_SOURCE=3` in release builds.
- Stack protector `-fstack-protector-strong`.
- PIE (`-fPIE`, `-pie`) for all binaries.
- RELRO + BIND_NOW: `-Wl,-z,relro,-z,now`.
- valgrind memcheck on release-candidate audit.

### 2.3 Banned functions

See JPL rule 30 above — enforced by `.semgrep.yml` custom rules +
`bugprone-unsafe-functions` in clang-tidy.

### 2.4 Dependency auditing

Enforced today:

- **Dependency Review**, **Gitleaks**, **Semgrep** and **CodeQL** run on every
  PR
  (`.github/workflows/security-scans.yml`; CodeQL uses the `security-extended`
  and
  `security-and-quality` suites from `.github/codeql-config.yml`).
- **Renovate** (`renovate.json`) opens dependency PRs on weekdays before 06:00
  Europe/Vienna and pins GitHub Actions to digests.
- `osv-scanner.toml` configures the OSV scanner.

Target scanners. These are named as goals; no workflow in the tree runs them as
a PR
gate yet: **trivy** (Dockerfile and filesystem CVEs) and **pip-audit** (Python
dependencies).

## 3. Quality gates (required for PR merge to master)

- ✅ `make lint` — zero clang-tidy / cppcheck / ruff / black findings
- ✅ `make test` — all unit + integration tests pass with ASan + UBSan
- ✅ `make sec` — zero semgrep findings (it runs `semgrep` with `.semgrep.yml`,
  the
  `p/cwe-top-25` and `p/cert-c-strict` rule sets). Gitleaks runs as the
  `Gitleaks` CI
  gate and as a pre-commit hook; bandit runs through the Python lint
  configuration.
- ✅ CodeQL — zero security-and-quality issues at `security-extended` suite
- ✅ Conventional commit messages (enforced by commit-msg hook)
- ✅ CI matrix green on Linux/macOS/Windows
- ✅ Netflix source-of-truth golden tests pass (CPU, 3 pairs: 1 normal + 2
  checkerboard)
- ✅ SYCL parity (ADR-1177) — when `SYCL_ARC_RUNNER_ENABLED=true`, the required
  Arc A380 lane runs the SYCL unit suite and the CPU↔SYCL `float_ssim` matrix
  cell; a missing or offline enabled runner fails loudly. Broader CUDA/SYCL
  sweeps remain explicit local or backend-specific test runs. See
  [development/cross-backend-gate.md](development/cross-backend-gate.md).
- ✅ Coverage ≥ 70% overall, ≥ 90% for critical code (validation, parsing,
  crypto-adjacent); floors live in `scripts/ci/coverage-check.sh`
  ([ADR-0922](adr/0922-coverage-ratchet-aggressive.md))
- ✅ Touched-file lint-clean rule (ADR-0141) — every hunk in the PR's diff
  against its merge base must be lint-clean to the fork's strictest profile;
  `// NOLINT` is reserved for load-bearing invariants and must cite the ADR or
  digest that forces it.
- ✅ State-tracking rule (ADR-0165) — every PR that opens, closes, or rules
  out a Netflix upstream report against the fork updates
  [`state.md`](state.md) in the same PR.
- ✅ FFmpeg-patches sync rule
  ([ADR-1240](adr/1240-ffmpeg-release-patch-lifecycle.md), rule 11 of the
  [agent hard rules](development/agent-hard-rules.md)) — every PR touching a
  libvmaf C-API surface, CLI flag, `meson_options.txt` entry, or any other
  interface that the in-tree `ffmpeg-patches/` patches consume updates the
  relevant patch file in the same PR.

### 3.1 Netflix golden-data gate

The fork preserves the three Netflix-authored reference test pairs as the
canonical
ground-truth gate for VMAF numerical correctness (CPU only):

| # | Type          | Reference                                      | Distorted                                      |
|---|---------------|------------------------------------------------|------------------------------------------------|
| 1 | normal        | `src01_hrc00_576x324.yuv`                      | `src01_hrc01_576x324.yuv`                      |
| 2 | checkerboard  | `checkerboard_1920_1080_10_3_0_0.yuv`          | `checkerboard_1920_1080_10_3_1_0.yuv` (1-px)   |
| 3 | checkerboard  | `checkerboard_1920_1080_10_3_0_0.yuv`          | `checkerboard_1920_1080_10_3_10_0.yuv` (10-px) |

YUVs live in `python/test/resource/yuv/` (gitignored; provision them with
[`scripts/test/fetch-test-yuvs.sh`](development/test-fixtures.md)). Golden expected
scores are hardcoded `assertAlmostEqual` assertions in `python/test/` (primarily
`quality_runner_test.py`, `vmafexec_test.py`,
`vmafexec_feature_extractor_test.py`,
`feature_extractor_test.py`, `result_test.py`). **These assertions are never
modified.**
They run in CI as a required status check on every PR. They are not run as a
pre-commit
hook because their runtime is longer than the acceptable pre-commit latency
budget.

`make test-netflix-golden` runs them against an isolated native build
([ADR-1317](adr/1317-golden-gate-build-isolation.md)).
`make test-netflix-golden-arm64` runs the same assertions against an aarch64
cross build under qemu-user, with GCC or clang
([ADR-1461](adr/1461-strict-fp-every-translation-unit.md),
[ARM backend guide](backends/arm/overview.md#running-the-golden-gate-for-aarch64-on-an-x86-host)).

Fork-added tests (SYCL, CUDA, SIMD snapshots, performance benchmarks) live in
separate
files and directories, and must not modify or override Netflix golden behavior.

## 4. Deterministic builds

- `SOURCE_DATE_EPOCH` respected throughout the build
- All build-time random data (tempdir names, etc.) is seeded from
  `SOURCE_DATE_EPOCH`
- Meson pinned to a specific minor version via `dev-setup` scripts
- Compiler/linker flags logged and archived per release

## 5. Supply chain

See the tracked [release](development/release.md) and
[repository-security](development/repository-security.md) runbooks:

- **SLSA v1 build provenance** on every release: a GitHub artifact attestation
  signed through Sigstore ([ADR-1356](adr/1356-release-provenance-attest.md))
- **CycloneDX + SPDX SBOMs** on every release
- **Keyless cosign** signatures via Sigstore / GitHub OIDC
- **Transparency log** (Rekor) makes signatures publicly verifiable

## 6. Compliance targets

- **OpenSSF Best Practices** — truthful Passing assessment first; Silver/Gold
  are
  later goals, not earned status. Registration and evidence are active work.
- **OpenSSF Scorecard** — unrounded ≥ 8.5 required on master, with exact-head
  full-report verification and explicitly separate PR file checks per
  [ADR-1247](adr/1247-scorecard-exact-head-gates.md) and the
  [operator guide](development/ossf-scorecard.md). Scanner errors fail; the
  precise no-release state remains visibly unassessed, never signed.
- **OWASP ASVS** — Level 2 applicable controls (auth/authz not applicable —
  library)
- **NIST SSDF** — PW.4 (design), PW.5 (implement), PW.7 (review), PW.9 (archive)
  aligned

## 7. Non-goals

We explicitly do **not** pursue:

- Full MISRA C compliance (too restrictive for numerical code — informative
  subset only)
- Claiming a Best Practices badge before its criteria and external status are
  verified; no calendar deadline substitutes for that evidence
- FIPS 140 (no cryptography in this library)
- DO-178C (aerospace software — out of scope)

## 8. Multi-language policy (ADR-0702)

VMAFX uses multiple languages. Each has a clearly bounded role:

| Language | Role | Constraint |
|---|---|---|
| **C / C++23** | Core library (`core/`) — metric engine, feature extractors, GPU backend runtimes | All of §1–§7 apply. C ABI at `core/include/libvmaf/` is frozen. New fork-added C files target C23; C++ files target C++23. Netflix-inherited C files are migrated per-TU only when a PR already touches the file. |
| **Go (1.27)** | Production CLI binaries and servers (`cmd/`) | Module root `github.com/VMAFx/vmafx`. `go vet` is a required CI gate. New packages must have `_test.go` coverage before merging. |
| **Rust (stable)** | libvmaf FFI bindings (`bindings/rust/vmafx-sys`) and optional feature-extractor pilots (`core/src/feature/rust/`) | `cargo clippy` + `cargo test` are required CI gates. `unsafe` blocks must be audited and explained in a doc comment. |
| **Python** | ML training (`ai/`), dev scripts (`scripts/`), MCP server scaffolding (`mcp-server/`), vmaf-tune Python harness (`tools/vmaf-tune/`) | ruff + mypy strict. No Python in hot-path scoring code. |
| **CUDA / SYCL / HIP / Metal** | GPU compute kernels, inside the C core | Language-specific style rules in `docs/backends/`. |

**Cross-language invariants:**

- The C ABI at `core/include/libvmaf/` is the single stable interface boundary.
  Go, Rust, and Python all consume it; none of them may call each other
  directly.
- A new language is never added to the project without a per-language CI gate
  (compile check + fast tests) and an ADR documenting the role.
- No production tooling binary embeds ML model weights or training code.
  That stays in `ai/` (Python only).

## 9. Revising this document

Changes to this document require:

1. A dedicated PR titled `docs(principles): <summary>`
2. One independent human approval after the latest push, as for every PR to
   `master` (`.github/CODEOWNERS` names a single owner; see
   [repository security](development/repository-security.md))
3. A corresponding change to tooling if the standard is newly enforceable in CI
4. Linked rationale — either a design doc, a referenced security incident, or a
   standards-body update
