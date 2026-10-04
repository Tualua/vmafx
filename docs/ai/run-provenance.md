<!-- markdownlint-disable MD060 -->
# Tiny AI — run provenance sidecars

Every tiny-AI script that writes a durable JSON report records a
`run_provenance` block, so a model, feature table or report can be traced back
to the exact inputs, arguments and outputs that produced it. This page lists
the block, the shared helpers, and which script writes which report. For the
training flow itself see [training.md](training.md). The convention is recorded
in ADR-0661.

## What the block records

`run_provenance` is intentionally compact:

| Field | Meaning |
|---|---|
| `schema` | Provenance schema name, currently `ai-run-provenance-v1`. |
| `entrypoint` | User-facing script path plus SHA-256 when the script file exists. |
| `argv` | Original command-line arguments after wrapper normalization. |
| `args` | Parsed arguments sorted into deterministic JSON values. |
| `inputs` | Named corpus, feature, metadata, or profile paths with existence and file hashes. |
| `outputs` | Named model, card, manifest, metrics, or report paths. Future outputs may be marked `missing` before they are written. |
| `shared_trainer` | Optional implementation script when a wrapper delegates to a shared trainer. |

## Add provenance to a script

Use the shared helpers in `aiutils.run_manifest`:

| Situation | Helper |
|---|---|
| Embed provenance in an existing stable report schema | `build_run_provenance()` |
| New standalone sidecar (the `schema` + adapter counters/config + `run_provenance` envelope) | `write_run_manifest()` |
| JSON file or report-style stdout, object- or list-shaped | `write_manifest_json()` (file) and `dumps_manifest_json()` (stdout) |

The Claude workflow for adding or auditing sidecars is
`.claude/skills/ai-run-manifest/SKILL.md`.

!!! note
    `write_manifest_json()` and `dumps_manifest_json()` serialize non-finite
    diagnostics (`NaN`, positive infinity, negative infinity) as JSON `null`.
    The file form also keeps the shared atomic-write guarantee. Empty or failed
    evaluation results therefore stay readable by strict RFC-8259 consumers,
    without changing their in-memory float semantics.

### CLI helper layer

Operator-facing scripts that fit the shared shape use `aiutils.cli_helpers`:

| Helper | Use |
|---|---|
| `make_argument_parser()` | Standard parser construction for AI scripts. |
| `collect_cli_argv()` | Canonical raw-argv capture before parsing. |
| `add_batch_manifest_arguments()` | Shared `--manifest`, `--base-dir`, report-output, fail-fast, and optional row-failure flags for batch materializers. |

Table-specific defaults, row schemas, and materializer options stay in the
individual scripts; the helpers only cover boilerplate that should not drift.

### Script bootstrap

Directly executable `ai/scripts/*.py` files call
`bootstrap_ai_script(__file__)` from `ai/scripts/_script_bootstrap.py` before
importing shared repo-local modules. It resolves the script path, repository
root, `ai/src`, `ai/scripts`, and the optional `tools/vmaf-tune/src` root
without copying ad hoc `sys.path.insert(...)` blocks into every script. Enable
only the roots the script needs:

| Bootstrap option | Use |
|---|---|
| default | Import `aiutils` from `ai/src`. |
| `include_repo_root=True` | Import repo-root packages such as `ai.data`. |
| `include_ai_scripts=True` | Import sibling materializers or feature extractors from `ai/scripts`. |
| `include_vmaf_tune_src=True` | Import `vmaftune` helpers for table materializers. |

## Which script writes which report

### Model training and export

| Scripts | Report | What the block identifies |
|---|---|---|
| `train_konvid_mos_head.py` (KonViD MOS) | model sidecar | entrypoint `ai/scripts/train_konvid_mos_head.py` |
| `train_chug_hdr_mos_head.py` (CHUG HDR MOS) | model sidecar | entrypoint `ai/scripts/train_chug_hdr_mos_head.py`, `shared_trainer` `ai/scripts/train_konvid_mos_head.py`: the CHUG command is the operator-facing contract, the training loop is shared |
| `train_fr_regressor.py`, `train_fr_regressor_v2.py`, `train_fr_regressor_v3.py` | model sidecars; v1 and v2 also write it into the metrics JSON | exact table path, arguments and output targets, so failed gates still preserve them |
| `vmaf_tiny_v2`, `vmaf_tiny_v3`, `vmaf_tiny_v4` exporters; C2/C3 KoNViD baselines; FastDVDnet pre-filter; TransNet V2; `fr_regressor_v2_ensemble_v1_seed*` | sidecar JSON | checkpoint, upstream weights, corpus, export command, gate verdict and output paths |
| `vmaf_tiny_v2` to `vmaf_tiny_v5` training scripts | `--out-stats` JSON | training parquet input(s), checkpoint target, stats target, argv, hyperparameters; the stats feed the ONNX exporters |
| `train_fr_regressor_v2_ensemble.py` (smoke and production) | `fr_regressor_v2_ensemble_v1.json` | corpus input, member ONNX outputs, registry target, manifest path |
| `train_fr_regressor_v2_ensemble_loso.py` | each `loso_seed{N}.json` | corpus JSONL, parsed training arguments, argv, per-seed report target |
| `validate_ensemble_seeds.py` | `PROMOTE.json` / `HOLD.json` | LOSO artifact directory, corpus root snapshot input, thresholds, seed list, verdict output path |
| `train_saliency_student.py`, `train_saliency_student_v2.py` (`--metrics-out`) | metrics JSON | DUTS-TR root, ONNX output, metrics output, parsed training arguments, argv |
| `train_predictor_v2_realcorpus.py` | `runs/predictor_v2_realcorpus/report.json` | selected codec list, corpus roots, resolved JSONL files, gate arguments, report target |
| U2NetP mirror exporter | `u2netp-mirror-export-manifest-v1` sidecar | the same block plus upstream checkout, checkpoint, license, NOTICE, output hash and ONNX metadata status |

Use the saliency block when comparing v1/v2 saliency refreshes or replaying a
DUTS-rooted training run from a model card.

### Evaluation and validation

| Scripts | Report | What the block identifies |
|---|---|---|
| `eval_loso_vmaf_tiny_v3.py`, `eval_loso_vmaf_tiny_v4.py`, `eval_loso_vmaf_tiny_v5.py`, `eval_multiseed_v3_v4.py` | report JSON | feature parquet input, parsed hyperparameters, argv, report target |
| `validate_vmaf_tiny_v2.py`, `validate_vmaf_tiny_v3.py`, `validate_vmaf_tiny_v4.py` (`--out-json`) | smoke-validator report | validated ONNX, feature parquet, PLCC/RMSE gate result, optional comparison model, argv, JSON target |
| `eval_loso_mlp_small.py`, `eval_loso_3arch.py` (legacy) | JSON and Markdown reports | Netflix corpus root, fold-checkpoint directory, baseline ONNX inputs, report targets |
| `eval_probabilistic_proxy.py` (legacy) | metrics output | ensemble manifest, optional held-out parquet, metrics output |
| `eval_saliency_per_mb.py` (legacy) | JSON report | predicted and ground-truth mask directories, JSON target |
| `validate_model_registry.py` (`--out-json`) | validation report | registry and schema inputs, cross-file validation result |
| `validate_saliency_student.py` (`--out-json`) | validation report | ONNX input, allowlist / parity / registry check verdicts |

Use the evaluation block when comparing refreshed LOSO or multi-seed numbers
instead of relying on shell history. The legacy reports stay comparable with
the refreshed v3/v4/v5 reports because they use the same schema.

### Feature tables, corpora and fetchers

| Scripts | Report | What the block identifies |
|---|---|---|
| `materialize_mos_labels.py`, `materialize_second_opinion_features.py`, `materialize_saliency_features.py` (`--audit-json`); `signal_mix_audit.py` (`--out-json`) | audit JSON | source tables, label/score inputs, saliency model inputs where used, output table targets, report targets |
| `extract_k150k_features.py`, `combine_full_feature_parquets.py`, `enrich_k150k_parquet_metadata.py` | `<out>.manifest.json` by default | source parquet/video/metadata paths, feature schema, backend split, filled feature columns, row counts, argv |
| `aggregate_corpora.py` | `<output>.manifest.json` (`--manifest-out`) | MOS scale-conversion metadata, source-shard inputs, dedup counters, corpus-source overrides |
| `merge_corpora.py` | `<output>.manifest.json` (`--manifest-out`) | required vmaf-tune corpus keys, natural dedup key, input shards, merge counters |
| `extract_full_features.py`, `konvid_to_vmaf_pairs.py`, `bvi_dvc_to_corpus_jsonl.py` | `<out>.manifest.json` / `<output>.manifest.json` | corpus and cache roots, VMAF/model inputs, feature lists or row schema versions, row/frame/clip counters, failed KoNViD clip IDs where applicable, adapter labels |
| `chug_to_corpus_jsonl.py`, `konvid_1k_to_corpus_jsonl.py`, `konvid_150k_to_corpus_jsonl.py`, `youtube_ugc_to_corpus_jsonl.py`, `lsvq_to_corpus_jsonl.py`, `live_vqc_to_corpus_jsonl.py`, `waterloo_ivc_to_corpus_jsonl.py` | `<output>.manifest.json` (`--manifest-out`) | corpus label, row counters, download/probe attrition, effective row caps, local corpus roots, manifest CSV inputs |
| `fetch_konvid_1k.py` | `<root>/fetch_manifest.json` | archive/source URLs, selection policy, output bundle paths |
| `fetch_youtube_ugc_subset.py` | `<manifest>.run-manifest.json` (its existing stem content manifest is preserved) | archive/source URLs, selection policy, output bundle paths |
| `build_bisect_cache.py` (`--manifest-out`) | manifest | cache mode, check status, target-column candidates, default feature columns, generated artifact counts |
| `collect_gpu_calibration_data.py` | `<output>.manifest.json` | selected features, backends, devices |
| `extract_ugc_features.py` | `<out-parquet>.manifest.json` | manifest/pair/fail/source counts |
| `extract_konvid_frames.py` | `ai/data/konvid_frames_manifest.json` | frame-pair materialization counts |

The BVI-DVC adapter also emits the current vmaf-tune v3 additive columns with
explicit unavailable defaults, so cached BVI rows stay schema-compatible.
The fetcher sidecars let a later JSONL or parquet manifest be traced back to
the original local download instead of an anonymous corpus directory.

Operational notes for `collect_gpu_calibration_data.py`:

- The default backend is CUDA and the default architecture label is
  `cuda:default`. Collect from SYCL hardware with
  `--backends sycl --arch-id <stable-device-id>`.
- Scorer output must be a JSON object with a `frames` array of frame objects.
  Malformed output is rejected before metric pairing.

### Analyses, calibration and quantisation

| Scripts | Report | What the block identifies |
|---|---|---|
| `vmaf-train` commands with `--json`: `validate-norm`, `profile`, `audit-learned-filter`, `quantize-int8`, `cross-backend`, `bisect-model-quality` | JSON report | CLI entrypoint, parsed thresholds and options, model and feature inputs, JSON/model output targets |
| `feature_correlation.py` (`--out`) | report JSON | source parquet, target column, redundancy threshold, top-K setting, report path, next to the Pearson / MI / LASSO / random-forest outputs |
| `phase3_subset_sweep.py` (`--out`) | subset result keys | source parquet, subset list, seed policy, standardization flag, report path |
| `calibrate_phase_f_recipes.py` (`--out`) | calibrated `vmaf-tune auto` recipe JSON | source corpus JSONL, optional row cap, argv, recipe output path |
| `calibrate_nr_threshold.py` (`--output`) | updated `nr_metric_v1.json` calibration sidecar | requested and actual corpus directories, `nr_metric_v1.onnx`, CRF grid, argv, Markdown calibration report path |
| `ptq_dynamic.py`, `ptq_static.py`, `qat_train.py` (`--report-out`); `measure_quant_drop.py` (`--out-json`) | quantisation report | fp32/int8 model paths, calibration/config inputs where applicable, size/gate statistics, argv, report path |

Prefer the quantisation reports for model-card evidence instead of terminal
logs. `calibrate_nr_threshold.py` quality-gates sidecar writes with
`--min-calibration-samples` and `--min-plcc`. A weak fit still leaves a
Markdown report for diagnosis, but does not update the tune-facing JSON unless
`--allow-weak-calibration` is set.

## Feature-correlation analysis rules

`feature_correlation.py` decides which columns it analyses, and records what it
skipped, so a report can be replayed from the file alone.

1. Only columns whose parquet-backed pandas dtype is numeric count as features.
   Text, boolean, categorical, object and decimal columns are skipped. A
   numerically encoded category is included, because the physical dtype, not
   the column name, is authoritative.
2. The report keeps input-schema order in `feature_cols` and records excluded
   columns in `skipped_non_numeric_columns`, `skipped_all_nan_columns` and
   `skipped_constant_columns`.
3. All-null numeric features are removed before complete-case filtering, so one
   unavailable metric does not erase every row (common in partially populated
   aggregate tables).
4. Remaining per-row missing or non-finite values (`NaN`, positive and negative
   infinity) are excluded across the selected features and target before NumPy
   or scikit-learn sees the matrix. A table with no usable numeric feature, or
   no finite complete feature/target row, fails with a direct diagnostic.
5. Constant numeric columns are excluded: their Pearson correlation is
   undefined, NumPy warns under the warnings-as-errors policy, and a
   zero-importance tie must not promote them into `consensus_topk`. The check
   repeats after complete-case filtering, because removing rows for a sibling
   feature can turn a varying column into a constant.
6. If optional scikit-learn analysis is unavailable, its method maps and top-K
   lists are empty rather than carrying non-standard JSON `NaN` placeholders.
7. `--redundancy-threshold` rejects non-finite spellings at argument parsing,
   and the assembled report is validated with strict RFC-8259 number semantics
   before the atomic manifest write.
