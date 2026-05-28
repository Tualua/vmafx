# aiutils — Shared AI Helpers

This package centralizes common utility patterns to reduce duplication across
`ai/scripts/` and `tools/vmaf-tune/src/vmaftune/` (and any other downstream
consumer that adds `ai/src` to `sys.path`).

## Invariants for new scripts and modules

When writing a new script in `ai/scripts/` or a new module in
`tools/vmaf-tune/src/vmaftune/`, follow these patterns:

1. **File hashing:** Import `sha256` from `aiutils.file_utils`, not a local `_sha256()`.
2. **UTC timestamps:** Use `now_iso_8601()` from `aiutils.time_utils` for ISO-8601
   second-precision UTC (not ad hoc `.isoformat()` calls).
3. **JSONL I/O:** Use `iter_jsonl()` from `aiutils.jsonl_utils` to read
   newline-delimited JSON and `dumps_jsonl_row()` to write JSONL rows. Do not
   hand-roll `json.dumps(row) + "\n"` in artifact-producing scripts.
4. **Atomic Parquet writes:** Use `write_parquet_atomic()` from `aiutils.parquet_utils`
   to safely write DataFrames with cleanup on failure.
5. **Run provenance / strict JSON artifacts:** Use
   `aiutils.run_manifest.write_run_manifest()` for
   script-specific sidecars that need stable entrypoint, args, input, and
   output metadata plus adapter-specific counts/config. Use
   `build_run_provenance()` only when embedding the provenance block into an
   existing report schema. Do not hand-roll path hashing or the manifest
   envelope in each script. Manifest JSON is strict RFC-8259 JSON:
   non-finite floats are serialized as `null`, never `NaN` or `Infinity`.
   Use `dumps_manifest_json()` for stdout JSON paths that expose the same
   report payload without writing it to disk. Use `write_manifest_json()` for
   legacy report/cache JSON artifacts that do not need a new provenance
   envelope but still need the same strict serialization boundary.
6. **CLI setup:** Use `make_argument_parser()` and `collect_cli_argv()` from
   `aiutils.cli_helpers` for new operator-facing scripts. Batch manifest runners
   must also use `add_batch_manifest_arguments()` so `--manifest`,
   `--base-dir`, `--report-json`, `--report-md`, `--fail-fast`, and optional
   `--allow-row-failures` stay aligned.
7. **Direct script bootstrap:** `aiutils` itself must stay importable without
   mutating `sys.path`. Directly executed `ai/scripts/*.py` entrypoints use
   `ai/scripts/_script_bootstrap.py` before importing this package; do not move
   that bootstrap into `aiutils` where it would be too late to solve the import.

## Module inventory

- `file_utils.py` — `sha256(path) -> str`
- `time_utils.py` — `now_iso_8601() -> str`
- `jsonl_utils.py` — `iter_jsonl(path) -> Iterator[tuple[int, dict]]`,
  `dumps_jsonl_row(row) -> str`
- `parquet_utils.py` — `write_parquet_atomic(df, output, **kwargs) -> None`
- `run_manifest.py` — deterministic `run_provenance` sidecar/stdout helpers
- `cli_helpers.py` — shared parser/raw-argv/batch-manifest argument helpers
