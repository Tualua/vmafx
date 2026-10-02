---
paths:
  - scripts/ci/tidy-ratchet.py
  - scripts/ci/tidy-baseline-*.json
  - scripts/ci/tests/test_tidy_ratchet.py
  - scripts/ci/tests/test_tidy_scoped_write.py
  - .clang-tidy
invariant: Baselines only via `--write` on the lane's own toolchain; counts only decrease; `HeaderFilterRegex` starts `(^|/)`.
---
<!-- markdownlint-disable MD013 MD060 -->
# tidy-ratchet.py invariants (ADR-1142)

- `scripts/ci/tidy-baseline-<lane>.json` generated only by `tidy-ratchet.py --write`
  (or `make tidy-ratchet-write`); never hand-edit count. Baseline may only
  decrease; raising number to make CI green = policy violation, not fix.
- Dedup key `(path, line, column, check)` and NOLINT rule ("cited" =
  `ADR-NNNN` on previous, same or next line, or anywhere in
  `/* ... */` block comment holding marker; `NOLINTEND` never counts) =
  load-bearing: baselines measured with exactly these rules, so
  changing either requires re-measuring every lane in same PR (`cpu`
  full baseline = CI's own `tidy-ratchet-cpu` artifact; ADR-1243 permits
  only guarded scoped tightening afterward).
- `Tidy Ratchet` job starts unconditionally, gates its
  work on ADR-1140 planner's `c_core` selector; `.clang-tidy`, this
  directory (ratchet + baselines) and workflow = CI-authority inputs, so
  editing any of them forces `mode=full`, lane runs. Never add
  `paths:` filter or custom early-skip probe to job.
- `clang-diagnostic-error` in any TU = measurement failure (exit 4), never
  zero. Build (generated headers) before measuring.
- **Measure on the lane's own toolchain, not the workstation's.** A full
  `--write` records what the measuring host sees, so a host whose libc/compiler
  differs from the lane bakes that host's diagnostics into the baseline. Seen
  on 2026-09-22: on gcc-16/glibc the `assert()` expansion makes
  `misc-static-assert` fire in `core/src/dict.cpp` (+1) and
  `core/src/feature/feature_collector.cpp` (+3) — neither file had changed —
  while the same tree on the lane's gcc-15 `Ubuntu 15.2.0-16ubuntu1` with
  clang-tidy 22.1.8 showed zero increases. `cc_version` in the baseline names
  the compiler to reproduce; the ratchet warns when the measuring compiler
  differs, and that warning means "stop", not "commit anyway". Reproduce the
  lane locally with the Ubuntu 26.04 dev image plus `clang-tidy-22` from
  apt.llvm.org, configured exactly as the workflow does
  (`CC=gcc-15 CXX=g++-15 meson setup build core -Denable_cuda=false
  -Denable_sycl=false -Db_lto=false`).
- **Build products never measured.** Everything under `--build-dir` = generated
  (xxd `src/*.json.c` + `src/brisque_live.model.c`, HIP `*_hsaco.c`,
  `config.h`); ADR-1142 exempts generated files. `load_compile_commands()`,
  `parse_diagnostics()`, `scan_nolints()` all drop paths under build dir
  (`build_dir_prefix()` / `is_build_product()`) -> in-tree `build/` and
  out-of-tree `$RUNNER_TEMP` measure same set. Baselines = checked-in paths
  only; `build*/` key in any baseline = stale measurement. Before this rule
  nightly in-tree `build/` saw `build/src/*.json.c: warnings 0 -> 2` x18 (run
  36308945712) against out-of-tree cpu baseline.
- **arm64 lane = cross lane (ADR-1283).** Build dir configured with
  `build-aux/aarch64-linux-gnu.ini`; nothing else in the tree compiles
  `core/src/feature/arm64/` or the `ARCH_AARCH64` bodies of `core/test/`, so
  no other lane's compile database holds them. `TIDY_RATCHET_EXTRA_arm64`
  must keep `--extra-arg=--target=$(AARCH64_TARGET)` and
  `--extra-arg=--sysroot=$(AARCH64_SYSROOT)`: drop the target and clang-tidy
  parses `<arm_neon.h>` / `<arm_sve.h>` as x86 and every NEON TU is exit 4;
  drop the sysroot and libc resolves against the host. `exclude_untidyable()`
  in `lint-and-format.yml` still excludes `^core/src/feature/arm64/` — that
  job's CPU-only `build/` genuinely has no command for those files; this lane
  is where they are measured.
- **Scoped writer (ADR-1243):** `--only` plus `--write` requires exact nonempty
  measured-TU coverage, original tool version/lane, no observed debt
  increase. Preserve every unselected TU/header entry and all full-report
  metadata; append explicit scoped provenance. Validate report/baseline aliases
  before output, replace validated baseline atomically. Full and scoped
  writers share resolved-path advisory lock; preserve baseline-drift checks
  around measurement/replacement, fail on unreadable NOLINT inputs. Diagnostic
  `--only` run is not full comparison. Keep failure/zero-tightening cases in
  `tests/test_tidy_scoped_write.py`; never make CI's full lane use `--only`.
- Promoted clang-tidy checks (`-warnings-as-errors`) remain counted debt. Only
  recognized promotion exit/summary may bypass nonzero-tool-exit guard;
  parse/compile failures still invalidate that measurement. Reports retain
  actual `measured_sources` and `compile_failures` so partial/error output
  never presented as successful whole-tree scan.
- ADR-1113's Pelorus mirror and ADR-1276's manifest-owned boundary are outside
  native-lint ownership.
  `pelorus-mirror-paths.txt` is the single exact-path exemption set consumed by
  the sync guard, format hooks, changed-file tidy gate, and `tidy-ratchet.py`;
  do not restore prefix/directory classification. Keep one shared
  `is_exact_pelorus_mirror()` predicate inside the ratchet for TU selection,
  header diagnostics, and legacy-baseline normalization. A scoped baseline
  write must preserve historical entries for this excluded scope; only a full
  generated write may remove them. Fix mirror diagnostics in Pelorus and
  re-pin; never edit the fixture or raise a baseline locally. Tests live in
  `test_pelorus_mirror.py`, `test_tidy_ratchet.py`, and
  `test_tidy_scoped_write.py`.

## Tidy ratchet counts headers via an absolute-path-safe filter (ADR-1265)

`.clang-tidy` `HeaderFilterRegex` starts `(^|/)`. clang-tidy matches ABSOLUTE
paths; a `^core/` anchor matches nothing, headers vanish as "non-user code",
ratchet reports 0 header findings forever. That was the state before ADR-1265.

Symptom of regression: `Tidy Ratchet` says every `*.h` went `N -> 0`, asks to
tighten. Do not tighten; restore the `(^|/)`.

CPU baseline = CI's `tidy-ratchet-cpu` artifact (clang-tidy 22, ubuntu-26.04),
never a local run with another clang-tidy. GPU lanes: local `make
tidy-ratchet-write LANE=<cuda|hip|sycl>`, advisory.
