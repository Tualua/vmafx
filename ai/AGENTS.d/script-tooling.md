---
paths:
  - ai/scripts/_script_bootstrap.py
  - ai/src/aiutils/cli_helpers.py
  - ai/src/aiutils/file_utils.py
  - ai/pyproject.toml
invariant: Direct AI scripts share bootstrap and CLI helpers; atomic writes via temp rename; sys.modules pre-registration.
---
<!-- markdownlint-disable MD013 MD060 -->
# AI script CLI helpers and execution contracts

- [ADR-0680](../../docs/adr/0680-ai-cli-helper-pattern.md) — **AI batch CLIs share parser boilerplate.** Use `aiutils.cli_helpers.make_argument_parser()` and `collect_cli_argv()` for new operator-facing scripts. Batch manifest runners must use `add_batch_manifest_arguments()` so manifest/report/fail-fast flags stay consistent while table-specific parser fields remain local to each runner.
- [ADR-0681](../../docs/adr/0681-ai-script-bootstrap-helper.md) — **Direct AI scripts share import bootstrap.** Use `ai/scripts/_script_bootstrap.py::bootstrap_ai_script(__file__)` before importing `aiutils`, sibling materializers, or optional `vmaf-tune` helpers from directly executable `ai/scripts/*.py` file. Do not add fresh ad hoc `sys.path.insert(...)` blocks unless helper lacks required import root and same PR extends its tests/docs.
- **BUG-048 legacy CLI restoration.** twelve eval/quant/export scripts listed in
  `ai/tests/test_ai_cli_helper_restoration.py` stay on both ADR-0680 and
  ADR-0681: no local `sys.path.insert`, direct `ArgumentParser`, or raw
  `sys.argv` handling. script importing repository's `ai.*` package must
  pass `include_repo_root=True` to `bootstrap_ai_script`; default only adds
  `ai/src` and cannot support direct `python ai/scripts/foo.py` invocation.
- [ADR-1097](../../docs/adr/1097-ai-script-atomic-writes.md) — **Cache and output file writes must be atomic.** All per-clip cache JSON writes in `ai/scripts/` and all final Parquet / JSONL output writes must go through `aiutils.file_utils.write_text_atomic` (for JSON/text) or `aiutils.parquet_utils.write_parquet_atomic` (for Parquet). Both helpers write to sibling temp file, rename atomically so crash mid-write never leaves partially-truncated file poisoning subsequent resume logic. Do **not** use `Path.write_text(...)` or bare `df.to_parquet(dest)` for any file resume loop tests with `path.is_file()`. `write_manifest_json` in `aiutils.run_manifest` also made atomic (transparent to callers). `extract_k150k_features._write_parquet_from_rows` uses same pattern as established precedent.

- **`ai/pyproject.toml` `pythonpath = ["scripts"]` required for batch
  materializer tests.** Tests loading `ai/scripts/batch_materialize_*.py`
  via `importlib.spec_from_file_location` trigger `_script_bootstrap` at
  module-load time. Without `ai/scripts/` on `sys.path`, those tests fail
  with `ModuleNotFoundError: No module named '_script_bootstrap'` when
  invoked from repo root. `pythonpath = ["scripts"]` entry in
  `[tool.pytest.ini_options]` = canonical fix (ADR-0991). Do not
  remove it and do not substitute `PYTHONPATH=ai/scripts` prefix in CI
  step definitions — pyproject config = single source of truth.
- **`importlib.util` callers must pre-register module in `sys.modules`.**
  Python 3.14 introduced regression in `dataclasses._is_type()`
  (CPython gh-129861): it calls `sys.modules.get(cls.__module__).__dict__`
  which raises `AttributeError: 'NoneType' …` when module not yet
  in `sys.modules` at `exec_module()` time. Any caller loading
  `ai/scripts/` module via `importlib.util.spec_from_file_location` +
  `module_from_spec()` + `exec_module()` **must** insert
  `sys.modules[spec.name] = module` between `module_from_spec()` and
  `exec_module()`. Invariant applies to all such loaders in test
  suite and any automation harness. `_script_bootstrap.py` itself avoids
  crash by not using `from __future__ import annotations` (which delays
  annotation evaluation, can trigger bug in dataclass field
  resolution).
