---
paths:
  - scripts/ci/gen-sycl-compile-commands.py
  - scripts/ci/clang-tidy-sycl.sh
  - scripts/ci/test_sycl_tidy_workflow_contract.py
  - scripts/ci/tests/test_*sycl*.py
invariant: SYCL TUs reach clang-tidy only through the generated compile database; `Tidy SYCL` is strict-required, always reports.
---
<!-- markdownlint-disable MD013 MD060 -->
# SYCL custom-command lint database

Meson emits per-feature SYCL translation units as `CUSTOM_COMMAND` rules
(`CUSTOM_COMMAND_DEP` + two `DEPFILE` lines when the target has a depfile:
feature and runtime TUs since PR #1764, test probes not), so
`gen-sycl-compile-commands.py` must augment the native compilation database
before clang-tidy can see them. Generator counts build statements compiling a
`.cpp` with icpx; fewer parsed -> exit 1, database untouched. Rule name or
layout changes again -> extend `SYCL_COMMAND_PATTERN`, never loosen the count
(a lane measuring no SYCL TU still reports clean:
`T-SYCL-TIDY-COMPDB-DEPFILE-RULE-2026-10-02`). Analyzer command drops
`-MD -MF <file>`: build's depfile stays the build's. Keep both legacy `-Xs` removal and the current
target-scoped pair (`-Xsycl-target-backend=spir64_gen` plus its following
backend argument) in the translator; passing either device-only option to
stock clang++ breaks the analyzer lane. `test_sycl_aot_command.py` is the
required pre-commit contract for the Meson AOT spelling, built-in language
standard policy, and translator output. Do not exempt a SYCL TU because it was
ported from upstream or predates the gate.

Running the generator is not left to the caller. `make tidy-ratchet` and
`make tidy-ratchet-write` expand `TIDY_RATCHET_COMPDB_$(LANE)` between the
native `write-compile-commands.py` export and the measurement; for `sycl` that
variable runs `gen-sycl-compile-commands.py`, and for `cpu` / `cuda` / `hip` /
`arm64` it is empty ([ADR-1290](../../../docs/adr/1290-sycl-tidy-lane-compile-database.md)).
Keep the hook in both targets and keep it ordered between the two: while it was
missing the lane measured zero SYCL feature TUs, and `tidy-baseline-sycl.json`
recorded an empty backend while still reporting the lane as clean.
`test_tidy_ratchet_sycl_compdb.py` pins that wiring. The GPU lanes also need
their build dir configured `-Db_lto=false` and placed outside the repository;
see the variable block in the `Makefile`.
`make tidy-ratchet LANE=sycl` sets `TIDY_RATCHET_EXTRA_sycl := --clang-tidy $(CURDIR)/scripts/ci/clang-tidy-sycl.sh`,
anchoring the wrapper to the worktree root. Under ADR-1270, `safe_subprocess.py`
requires allowlisted executables to be bare binary names or absolute paths;
relative paths with slashes fail validation. In addition, `tidy-ratchet.py`'s
`resolve_clang_tidy()` resolves multi-component relative binary paths to absolute
paths before measurement and execution. Preserve both the `$(CURDIR)` anchoring
in `Makefile` and `resolve_clang_tidy()` in `tidy-ratchet.py`.

In CI, the `clang-tidy-sycl` job (`Tidy SYCL`) in `lint-and-format.yml` has been
a required, non-advisory merge gate since `6475fa9ea` (ADR-1297). It belongs to
both `required` and `strictMustReport`: the detect step can skip work, but the
job has no path filter and must always report. Each pull-request, push-fallback,
normal-push, and dispatch command covers all changed SYCL sources, headers
(`.cpp`, `.hpp`, `.h`), and tests independently. The coupling between
`lint-and-format.yml`, `required-aggregator.yml`, and `rule-enforcement.yml` is
pinned fail-closed by `test_sycl_tidy_workflow_contract.py` using the shared
`required_aggregator_harness.py` driver. The exact strict-required set is also
pinned by `tests/test_hiss_replay_contract.py`; update that replay contract when
a reporting-always context legitimately joins or leaves `strictMustReport`.
Every workflow that reports a strict context lists `ready_for_review` in its
`pull_request` types. The aggregator ignores check runs older than its own run,
so without that type a PR opened as a draft (every Renovate PR) keeps only
draft-era runs and fails with "never reported". The same test pins this for
each strict context.

## Synthetic SYCL compile database

`gen-sycl-compile-commands.py` converts Meson's `icpx` custom commands into
stock-Clang commands for the SYCL tidy lane. Remove only device-compilation
arguments that stock Clang cannot parse, and translate compatible spellings
such as `-fp-model=` to `-ffp-model=`. Preserve every diagnostic option,
including `-pedantic`, `-Wall`, `-Wextra`, and `-Werror`; analyzer noise is a
defect to fix, not a reason to weaken the generated command. Keep
`tests/test_gen_sycl_compile_commands.py` paired with translator changes and
wired through the `test-sycl-compile-command-generator` pre-commit/pre-push
hook; an unwired regression test protects no CI lane.

## Workflow coupling

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `test_sycl_tidy_workflow_contract.py` | `rule-enforcement.yml` — `Verify SYCL required clang-tidy contract`; `.pre-commit-config.yaml` — `test-sycl-tidy-workflow-contract` | Enforces that `Tidy SYCL` in `lint-and-format.yml` is a strict-must-report, non-advisory required gate (`# required-aggregator`, no `continue-on-error`, full `.h`/`.cpp`/`.hpp` SYCL source and test coverage in every event branch). Uses the shared aggregator harness to prove failure or absence blocks merge while success passes. |
