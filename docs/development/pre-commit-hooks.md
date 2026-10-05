<!-- markdownlint-disable MD013 -->
# Local Git hooks

Run `make install-hooks` from the checkout or linked worktree you use.
The installer requires Python with `pre-commit` installed in the active
environment and prepares the configured hook environments before replacing
any hooks. `make hooks-install` remains an alias.

```bash
python3 -m pip install pre-commit
make install-hooks
```

The installer writes regular dispatcher files to Git's effective hooks
directory, including a configured `core.hooksPath`. Each invocation finds
its current worktree with Git. Removing the worktree used for installation
therefore cannot break hooks in the surviving checkout.

## Who owns which hook

Since [ADR-1249](../adr/1249-praetor-governance-adoption.md), [`lefthook.yml`](../../lefthook.yml) owns the `pre-commit` and `pre-push` hooks and the `post-commit` state sync. Its `framework-hooks` commands delegate both stages to the pre-commit framework, so every check in `.pre-commit-config.yaml` still runs; lefthook adds the praetor governance commands (context, audit, HISS evidence). The dispatchers that `make install-hooks` writes (ADR-1241) keep `commit-msg` and `pre-rebase`. ADR-1249 records that `make install-hooks` refuses to run once lefthook owns `pre-commit` and `pre-push`, because it treats them as custom hooks; check that ADR for the current state before relying on the installer in a checkout that already has lefthook hooks. The sections below describe the installer and the framework checks, which apply to both setups.

## Installed checks

| Git event | Framework mode, the default | Native mode |
| --- | --- | --- |
| `pre-commit` | Configured formatters and checks | Three native formatters |
| `commit-msg` | Configured Conventional Commits validation | Same framework check |
| `pre-push` | All configured push checks | Same framework checks |
| `pre-rebase` | Agent worktree drift guard | Same guard |

The dispatcher forwards Git's arguments and pushed-ref input to
`pre-commit hook-impl`. The framework selects checks and changed files
from `.pre-commit-config.yaml`. Push checks include assertion density,
twin drift, mypy, bounded-process regressions, FFmpeg patch replay, PR
deliverables, and MkDocs strict validation. The PR-body check may skip a first
push or draft PR;
that does not skip the other checks. Existing framework `.legacy` hooks
continue to run in framework stages.

A selected documentation push requires MkDocs. Missing `mkdocs` blocks the
push with an installation hint; install `docs/requirements.txt` in the
active environment. Direct non-doc invocations skip before requiring the
docs toolchain. The required hosted `Docs` job installs these dependencies
and runs strict validation.
Missing `pre-commit` itself blocks framework hook dispatch with a clear
message; activate the environment used for installation.

### Hook environments install outside the commit's git environment

In a linked worktree, Git exports an absolute `GIT_INDEX_FILE` to hooks.
When the framework (re)installs a `language: node` hook environment, it runs
`npm install -g git+file://<hook repository>` with that variable set, and
npm's checkout writes the hook repository's tree into the worktree's index.
The commit then fails or records the wrong tree. The pre-commit project does
not plan a fix ([pre-commit/pre-commit#3609](https://github.com/pre-commit/pre-commit/issues/3609)).

The `framework-hooks` entries of `lefthook.yml` (`pre-commit` and `pre-push`)
therefore run `pre-commit install-hooks` with `GIT_INDEX_FILE`, `GIT_DIR`,
`GIT_WORK_TREE` and `GIT_OBJECT_DIRECTORY` unset before `run` / `hook-impl`,
which keeps the commit's own environment because it needs the index. A failed
install blocks the commit or push. With a warm cache the extra call takes
well under a second. `python3 scripts/githooks/tests/test_install_hooks_env.py`
reproduces the defect in a throwaway linked worktree with a node hook, and
`make` runs it with the other hook tests; it skips, naming the missing tool,
when `npm` or `pre-commit` is absent.

## Post-commit private-state synchronization

The post-commit state hook resolves both the active worktree and Git's common
directory. In a linked worktree it mirrors the six canonical ledgers into a
regular ignored `.workingdir`, runs Praetor against the committing worktree,
and atomically publishes the resulting `STATE.md` to the canonical checkout.
The mirror preserves worktree-local caches and evidence; those directories are
not public documentation and are never copied back.

Synchronization is serialized by a lock in the common Git directory. Lock
contention, missing or non-regular canonical ledgers, a symlinked local state
root, and a missing synchronizer all fail the hook. A failed hook therefore
cannot silently record the main checkout's branch on behalf of an agent
worktree. See [ADR-1280](../adr/1280-worktree-state-sync.md) and its
[research digest](../research/1280-worktree-state-sync.md).

Run the helper directly from the checkout whose identity should be recorded:

```bash
scripts/githooks/state-sync.sh
```

## Python push scope

The `mypy-local` hook implements the touched-file rule in
[agent hard rule 10](agent-hard-rules.md): every added, copied, modified,
renamed or type-changed `*.py` path under `ai/` and `scripts/` in
`git diff origin/master...HEAD` is checked. Deleted paths are omitted.
The strict settings in `pyproject.toml` still apply. Fetch `origin/master`
before validating a rebased branch; a missing merge base blocks the push.

The hook runs once on every push and derives this complete set itself.
Pre-commit's old-remote-tip/new-tip file list can include unrelated changes
from master and omit an unchanged branch-owned file whose imports changed
after a rebase. Neither omission may narrow the check. This does not expand
the explicit file policy to other Python packages or unchanged master files.
Mypy's normal import checking still applies to the selected sources.

### What makes the hook fail

The hook reports only findings the branch introduces
([ADR-1261](../adr/1261-mypy-pre-push-delta-gate.md)). It checks the selected
files, then checks the same files again as they are at the merge base in a
disposable worktree, and lists what is new. Line numbers are left out of the
comparison, so inserting a line above an existing finding does not make it
look new. The output ends with a count of the findings that were inherited and
therefore not reported.

Two consequences worth knowing:

- A file that already fails still fails for its own reasons, and you may edit
  it. Adding a *different* finding to it is reported; the ones that were
  already there are not.
- A non-zero exit with nothing to attribute to a file is treated as mypy
  breaking, and blocks the push.

The delta keeps inherited debt from blocking unrelated changes. The required
hosted `Python Lint` job and the local hook both run this same gate; neither
uses a separate whole-tree exception list. Pull requests compare with the
fetched `origin/master`, while a master push supplies the event's exact prior
commit through `VMAFX_MYPY_BASE_REF`, so post-merge CI checks the Python paths
that actually landed rather than comparing `HEAD` with itself.

Both blocking callers pass `--no-site-packages` and
`--disable-error-code=import-not-found`. Its result therefore does not depend
on arbitrary PEP 561 packages in the active environment. Selected repository
sources, repository imports that resolve, and standard-library types are still
checked. The hosted job installs the same hash-locked mypy toolchain from
`requirements/locks/mypy.txt`; it does not install or run the training stack.
This isolation prevents a newer ambient stub from changing the gate or crashing
it before any attributable finding is emitted.

Files under `ai/src/` are checked in a separate run with
`--explicit-package-bases`. That directory is a `mypy_path` base, so without
the flag mypy sees each file under two module names and refuses the run
outright.

Run the same check manually from the repository root:

```bash
python3 scripts/git-hooks/pre-push-mypy.py
```

CI may select an explicit comparison authority without changing the local
default:

```bash
VMAFX_MYPY_BASE_REF=<previous-commit> \
  python3 scripts/git-hooks/pre-push-mypy.py
```

The checked-out HEAD must match the outgoing commit supplied by pre-commit;
push a different branch from its own checkout. Missing Git, mypy or selected
files blocks validation. Internal symlinks retain their Git filename for
selection and checking; their target must resolve to an existing regular file
inside the checkout. External, dangling, looping or directory targets fail.
The command always derives its own scope; filename arguments do not narrow it.

## Copyright and SPDX hook, and the declared exception list

`check-copyright` runs `scripts/ci/check-copyright.sh` on every tracked file whose
extension is `c h cpp cxx cc hpp hxx cu cuh hip metal mm go py pyx rs sh`
(pre-commit's `types_or` cannot select `.hip` or `.metal`, so the hook uses one
`files:` regex; keep it equal to the script's `case` lists). It requires:

| Rule | Files | Needs, in the first 40 lines |
| --- | --- | --- |
| ADR-0105 | `c h cpp cxx cc hpp hxx cu cuh hip mm metal` | a `Copyright` line |
| ADR-1250 | every extension above | an SPDX licence identifier line |

The hook excludes one thing, `scripts/ci/exact_twins.d/` (parity-gate data
fragments that only borrow the `.hip` extension). A file that cannot meet a rule
is not skipped by path: it is named in the **declared exception list**,
`.config/lint-exceptions.d/<rule>.toml`, where `<rule>` is `spdx` or
`copyright`. One entry is one tracked file with a reason and an expiry:

```toml
[[exception]]
path = "core/src/interop/pelorus_version.c"   # one tracked file, never a pattern
reason = "Read-only mirror of VMAFx/pelorus (ADR-1113); ..."
expires = 2026-12-31
```

An entry stops holding on its `expires` date: the file is read again and the hook
fails on it, and `check-lint-exceptions` (always run, also in CI's
`pre-commit run --all-files`) names the entry. It also fails on a missing field, a
rule that differs from the file name, a path that is a pattern or not tracked, a
duplicate, and an expiry more than 400 days out. Fix the file when you can; renew
an entry only with its reason still true.

```bash
python3 scripts/ci/lint_exceptions.py check
pre-commit run check-copyright --all-files
```

Entries today: ten files of the Pelorus mirror (`spdx`, until the line exists in
Pelorus and the mirror is re-vendored), two praetor-managed files (`spdx`) and
eight third-party MEX sources of the Netflix MATLAB harness (`copyright`).

## GitHub Actions workflow validation

Workflow files under `.github/workflows/` are validated against
`.github/actionlint.yaml` using `actionlint` pinned to `v1.7.12`
(HISS-11 hermetic supply chain pin).

The pre-commit hook runs on staged workflow files:

```bash
pre-commit run actionlint --all-files
```

To validate workflows and composite actions across the repository without
pre-commit, run:

```bash
make lint-actions
```

### Composite actions

actionlint reads workflow files only; given an `action.yml` it fails with
`"jobs" section is missing`. The composite actions under `.github/actions/*/`
get their own two checks, run by the same pre-commit job in CI:

| Hook | Reads | Checks |
| --- | --- | --- |
| `check-github-actions` ([check-jsonschema](https://github.com/python-jsonschema/check-jsonschema) `0.38.2`) | `.github/actions/*/action.yml` | The GitHub action manifest schema |
| `check-composite-actions` (`scripts/ci/check_composite_actions.py`) | every manifest under `.github/actions/` | `runs.using: composite`, a `description` on every input, one of `run` or `uses` per step, a `shell` on every `run` step; every `bash` and `sh` `run:` block goes through shellcheck with each `${{ ... }}` replaced by a placeholder and the same ignore list actionlint uses for a workflow block (`SC1091`, `SC2194`, `SC2050`, `SC2153`, `SC2154`, `SC2157`, `SC2043`) |

A step in another shell (`pwsh`, `cmd`, `python`) is printed as skipped with the
reason `no checker for this shell`; the repository has none today.
`test-check-composite-actions` keeps positive, negative and boundary cases for the
script (an unquoted variable, a missing `shell`, a missing input description, a
non-composite action, an unreadable manifest and an empty `.github/actions/`
each fail). Run the script alone with:

```bash
python3 scripts/ci/check_composite_actions.py
```

## Existing hooks and migration

The installer recognizes its own dispatchers, unmodified framework
hooks, and the repository's historical source symlinks, including links
into deleted `.claude/worktrees/` directories. It retains replaced
managed hooks in uniquely named `HOOK.vmafx-backup-*` files. Repeating an
unchanged installation makes no new backups.

Unknown regular hooks and symlinks cause installation to stop before
changing any hook. Review the reported paths and relocate or integrate
your custom hook explicitly before retrying. Existing backups are never
overwritten. Merely fetching the fix does not repair an already dangling
symlink: rerun `make install-hooks` from a checkout containing the fix.

Dispatch uses each active worktree's checked-out configuration. Installing
from an updated worktree repairs hook lifetime everywhere, but an older
branch does not acquire newly registered checks until its source config
is updated. In particular, older configs still lack the MkDocs push entry.

For inspection without installing:

```bash
git config --show-origin --get core.hooksPath
git rev-parse --path-format=absolute --git-path hooks
```

## clang-format reads `.hip` and `.metal`

The `clang-format` hook (pin `v23.1.2`, `.clang-format` at the root) reads C, C++
and CUDA by file type and HIP and Metal by extension: pre-commit's `types_or`
has no tag for either, so a second entry, `clang-format-hip-metal`, selects
`\.(hip|metal)$`. Both entries go through `scripts/ci/pelorus_mirror.py`, so the
Pelorus mirror stays exempt. The one exclusion is `scripts/ci/exact_twins.d/`:
those `*.hip` files are parity-gate data fragments ([ADR-1428](../adr/1428-exact-twins-fragments.md))
that only borrow the extension.

| Reader | Files |
| --- | --- |
| hook `clang-format` | C, C++, CUDA by type |
| hook `clang-format-hip-metal` | `*.hip`, `*.metal` |
| `make format`, `make format-check` | `CLANG_FORMAT_FILES` in the `Makefile`: `*.c *.h *.cpp *.hpp *.cu *.cuh *.hip *.metal`, minus the fragments and the mirror |
| native hook (`VMAFX_NATIVE_HOOKS=1`) | the same extensions, staged files |

```bash
pre-commit run clang-format clang-format-hip-metal --all-files
make format-check
```

Formatting a kernel moves line breaks only. For a `.hip` file, prove it with the
device assembly (`hipcc -S --cuda-device-only --offload-arch=gfx1036 ...`, the
random `__hip_cuid_*` symbol masked) before and after; for a `.metal` file, which
only builds on macOS, compare the files with all whitespace removed.
`test_clang_format_scope.py` keeps the selection, the Makefile list and the native
regex in step.

## Native formatting option

```bash
VMAFX_NATIVE_HOOKS=1 make install-hooks  # opt in
make install-hooks                      # restore the default
```

Native mode preserves the formatter-only choice in
[ADR-0924](../adr/0924-native-pre-commit-hooks.md). It runs installed
`ruff check --fix`, `clang-format -i`, and `shfmt -w` on matching staged
paths and restages files changed by formatters. Missing formatters print
a notice. Its pre-commit stage does not run the framework's security,
metadata, or agent-drift checks; use the default for those local checks.
Commit-message validation and push checks still use the framework in
both modes. CI uses the full framework configuration.

The native formatter reads working-tree files and restages the whole file
when it changes one. Use the framework path for partially staged files to
preserve the unstaged portion through the framework's stash/restore flow.

## black and ruff read every Python file

The `black` (pin `26.10.0`) and `ruff-check` (pin `v0.16.10`) hooks select files by
type, `python` and `pyi`, with no path filter: every tracked `.py`, `.pyi` and
extensionless Python script is read, wherever it lives (`python/`, `compat/`,
`core/test/`, `mcp-server/`, `dev-llm/`, `testdata/`, `.config/`, ...). The only
files left out are the **declared exceptions**,
`.config/lint-exceptions.d/black.toml` and `.config/lint-exceptions.d/ruff.toml`:
one tracked file per entry, with a reason and an expiry (format and rules:
`scripts/ci/lint_exceptions.py`). Today these are the praetor-managed
`tools/figures/mkdocs_hook.py` and `.config/agent/hooks/block_evasion.py`
(`praetorctl audit` compares them with praetor's own bytes) and five HISS scanner
fixtures that are a defect by design.

The hook `exclude` regexes, the `extend-exclude` of `pyproject.toml` and the list
name the same files; `test-python-format-scope` (always run) fails when they
differ, when a listed file no longer fails its tool (a stale entry), or when an
entry is past its expiry. `make lint-py`, `make format` and `make format-check`
run `ruff check .` and `black .`, which read the same `extend-exclude`; the native
pre-commit hook runs `ruff check --force-exclude` on every staged Python file.

```bash
pre-commit run black ruff-check --all-files
make lint-py
```

## Regression checks

```bash
python3 scripts/githooks/tests/test_install.py
python3 scripts/git-hooks/test-pre-push-mypy.py
python3 -m unittest scripts.lib.test_safe_subprocess \
  scripts.ci.tests.test_agent_eligibility_precheck
```

This runs real Git commits and pushes to disposable local repositories.
It tests installer-worktree deletion, failed commit/message/push gates,
first-push and draft documentation failures, custom-hook refusal,
framework migration, legacy hooks, native mode, and the rebase guard.
The required `Pre-Commit` CI job runs the same fixture before the normal
file checks. `make lint-sh` also runs it.

The mypy fixture exercises a real rebase and the installed pre-commit
framework, including an empty outgoing file list, type changes, safe and
unsafe symlinks, ref mismatches and missing prerequisites. Its registered
pre-commit/pre-push check also runs in required `Pre-Commit` CI.

See [ADR-1241](../adr/1241-worktree-hook-dispatch.md) and the
[research digest](../research/1241-worktree-hook-dispatch.md).

## Disposable Git fixture safety

Git exports repository and index variables to hooks. Changing directory or
using `git -C` does not override them, so a test that creates a temporary
repository must clear inherited `GIT_*` before its first Git command and
disable caller system/global Git configuration. The FFmpeg replay/smoke,
dependency-classifier, Level Zero and agent-cleanup fixtures use this isolation
for setup and assertions as well as the operation under test.

Run their caller-preservation regression with:

```bash
python3 scripts/ci/test_git_fixture_isolation.py
```

It creates fake caller repositories with committed, staged and unstaged work,
then runs each fixture with `GIT_DIR`, `GIT_COMMON_DIR`, `GIT_WORK_TREE`,
`GIT_INDEX_FILE` and `GIT_CONFIG_PARAMETERS` individually and together.
Every fixture must succeed without changing any caller metadata or files.
A second regression invokes a real Git hook from a disposable linked worktree.
Git supplies `GIT_DIR` itself; the old unisolated `git init` control changes
the fake caller's shared `core.bare`, while the current Level Zero test must
preserve both worktrees and all shared Git metadata byte for byte. This tests
the hook environment even when the invoking shell has no Git variables.

Only temporary caller paths are injected. The local pre-commit/pre-push hook
runs when its inputs change. Required `Pre-Commit` CI also runs the regression.
