---
paths:
  - ai/scripts/extract_k150k_features.py
  - ai/tests/test_extract_k150k_features.py
invariant: Parquet written at-end only; staging file WAL; scratch on /dev/shm; fail-loud on empty frames; misuse guard.
---
<!-- markdownlint-disable MD013 MD060 -->
# K150K-corpus extraction (ADR-0362, ADR-0382, ADR-0431)

**Script:** `ai/scripts/extract_k150k_features.py`
**Branch:** `chore/ensemble-kit-gdrive-quickstart`

## Rebase-sensitive invariants

- **Parquet writes at-end only — never per-flush (Research-0135 Win 1).**
  Rows accumulated in memory throughout run, appended to JSONL staging file
  (`<out>.rows.jsonl`) for crash durability. Parquet written exactly once at
  end via `_write_parquet_from_rows`. Old `_flush_parquet` helper (which read
  growing parquet on every 200-clip flush) removed. Restartability still
  guaranteed by `.done` checkpoint file; staging file adds second durability
  layer so rows aren't lost on unclean exit. Do not re-introduce per-flush
  parquet writes — they make total parquet I/O O(N²) over corpus size.
- **Staging file is main-process-only (Research-0135).**
  `_append_row_to_staging` called only from main process inside   `as_completed()` loop, after `fut.result()` returns. Worker subprocesses
  must never write to staging file. Violating single-writer semantics on
  staging file would corrupt it without error.
- **ffprobe skipped when sidecar has geometry (Research-0135 Win 2).**
  `_geometry_from_sidecar(meta)` reads `chug_width_manifest`,
  `chug_height_manifest`, `chug_framerate_manifest`, and
  `chug_bit_depth` from CHUG JSONL sidecar row. Sidecar metadata is
  loaded in `_load_jsonl_metadata`, which must include `chug_bit_depth` in its
  `keep` allowlist so 10-bit clips decode as `yuv420p10le`. Any required
  field absent (K150K-clips, incomplete rows) -> function returns `None`
  and `_probe_geometry(mp4)` called as fallback. Do not remove fallback —
  K150K-clips have no sidecar.
- **Scratch directory is auto-selected to `/dev/shm` when available (Research-0135 Win 3):**
  `_choose_scratch_dir(requested)` returns `/dev/shm/k150k_yuv_scratch` when `/dev/shm`
  is writable and `statvfs` reports >=20 GiB free; otherwise falls back to OS temp
  directory. threshold (20 GiB) covers 8 concurrent workers each holding   ~1.5 GiB 1080p 10-bit 240-frame YUV clip. Pass `--scratch-dir` to override.
  Do not lower 20 GiB threshold without updating headroom analysis in Research-0135.
- **Binary requirement:** script requires `core/build-cpu/tools/vmaf`
  (fork build); system `/usr/local/bin/vmaf` v3.0.0 lacks `ssimulacra2`
  and `motion_v2`. `--vmaf-bin` default (in `main()`) now points to
  `core/build-cpu/tools/vmaf`. Do NOT switch to `build-cuda/tools/vmaf`
  as default — CUDA binary has latent CLI double-write bug when
  `--feature <x>` is combined with auto-loaded default VMAF model
  (see Research-0096 / ADR-0382 for details).
- **CUDA split invariant:** operators explicitly passing CUDA-capable
  `--vmaf-bin` -> script must use explicit CUDA extractor names for   GPU-safe pass and `--cpu-vmaf-bin` for residual CPU pass
  (`float_ssim`, `cambi`). Do not re-collapse this into one generic
  `--backend cuda` invocation; CHUG/K150K 10-bit clips can fail
  `context could not be synchronized` through that path.
- **Parallelism model:** script uses `concurrent.futures.ProcessPoolExecutor`
  with `--threads-cuda` workers (default 8). Each worker fully independent.
  `--threads-cuda` flag named for historical reasons; controls outer
  process parallelism for both CPU and split CUDA modes. Do not switch to
  threading — libvmaf subprocess invocations not thread-safe for concurrent
  parallel pipelines.
- **Checkpoint thread-safety:** `_append_done()` called only from main
  process (after `fut.result()` returns in `as_completed()` loop). Do not
  call it from worker processes — append-only guarantee relies on single-writer
  semantics.
- **NaN propagation:** `ciede2000` and `psnr_hvs` return `null` from vmaf
  when ref == distorted (identity pair). All-NaN columns are **expected** —
  do not treat them as extraction failures. `np.errstate(all="ignore")`
  in `_aggregate_frames()` suppresses numpy warning; preserve it.
- **Column-order lock:** `FEATURE_NAMES` (line ~121) defines 21-feature
  column order (parquet schema v2) downstream loaders depend on. Appending
  is safe; reordering or removing entries breaks existing parquets and any
  trained model that consumed them. Increment parquet schema version in   separate ADR if reordering becomes necessary. **Schema v2 invariant
  (ADR-0431):** `ssimulacra2` omitted from K150K/CHUG self-vs-self
  extraction. In identity pairs (ref == distorted) it produces   constant ~100, yielding zero training signal while consuming
  30–50% of GPU time per clip. Operating in FR-from-NR mode (same
  video on both sides) -> all difference-based metrics
  (difference-based ssimulacra2, ciede2000, psnr_hvs, ADM, VIF)
  degenerate; see ADR-0362 §Negative consequences. CPU-only
  ssimulacra2 extraction remains available for genuine FR pairs
  where it is informative.
- **FEATURE_NAMES completeness invariant:** all `FEATURE_NAMES` entries
  must map to JSON keys emitted by pipeline (CUDA extractors, CPU
  residual, or `--model` dispatch). `vmaf` entry = model composite score
  emitted via `--model` arg in `_run_feature_passes`; all other entries
  = raw features emitted via `--feature` arguments.
- **vmaf column computed via vmaf_v0.6.1 (Research-0135):** `vmaf`
  column in CHUG/K150K output parquets is computed by dispatching   SDR `vmaf_v0.6.1` model via `--model version=vmaf_v0.6.1` in   libvmaf CLI invocation. Model is SDR-trained and mis-calibrated on PQ
  HDR clips; scores valid for relative bitrate-ladder comparison within
  content group but not meaningful as absolute HDR quality targets.
  Replace with Netflix HDR model when it ships (change `--model` arg in
  `_run_feature_passes`; no schema change required). Do NOT remove   `--model` arg without ADR — vmaf relationship across ladder rungs
  is required training feature per user direction 2026-05-16.
- **Checkpoint format:** `.done` file is append-only, one clip name per
  line, no header. Changing format without migration breaks in-progress
  runs. `_load_done_set()` / `_append_done()` helpers = single-exit-point
  for reads and writes; add any format change there.
- **`.done` = authoritative ledger; parquet row count must match on
  restart (ADR-0862).** Restart no-op branch in `main()` compares
  `len(done_set)` against `_parquet_row_count(args.out)` plus rows
  recovered from JSONL staging file. Raises `RuntimeError` on
  mismatch instead of silently writing `status=complete-noop` and
  returning 0. Do not weaken this check to warning. Do not
  auto-truncate `.done`. Do not skip it under any `--allow-*` flag.
  Silent confirmation of row deficit = exact failure mode this
  guard prevents (Bug-3 RCA 2026-05-30 lost ~92 K rows). End-of-run
  write path has matching `len(rows) == len(recovered_rows) + ok`
  assert; preserve it through any future refactor of
  `as_completed` accounting loop.
- **fsync parquet before unlinking staging (ADR-0862).** Both   no-op branch and end-of-run write path call `_fsync_path(args.out)`
  AFTER parquet rename(2) and BEFORE
  `staging_path.unlink(missing_ok=True)`. Helper fsyncs file
  and its parent directory so rename(2) is durable before   companion unlink can race ahead of it on power loss. Do not reorder
  these calls or drop `fsync` — staging-as-WAL design depends
  on parquet being durable when WAL is discarded.
- **JSONDecodeError surface (ADR-0862).** `_load_staging_rows`
  reports count of malformed lines to stderr as WARNING.
  Do not revert this to silent `continue`: truncated-tail
  staging file = leading indicator worker died mid-write,
  and operator needs to know.
- **Gitignore:** `runs/full_features_k150k.parquet` and
  `runs/k150k_extract.log` are gitignored (152K-clip output not tracked).
  Do not commit these files.
- **FR-from-NR adapter:** script does NOT call `NrToFrAdapter` from
  Python training harness — builds vmaf CLI argv directly with
  ref == distorted, which is lighter-weight equivalent. Any upstream
  refactor of Python adapter is irrelevant to this script.
- **FR-corpus misuse guard (ADR-0509):** script = no-reference adapter.
  Running it on full-reference corpus (CHUG: `chug_ref==1` references
  paired with bitrate-ladder distortions for same
  `chug_content_name`) silently produces parquet where every clip is
  scored against itself. Every difference-based metric collapses to
  its identity-pair floor (`adm2 == vif_* == 1.0`, `psnr_y == 60`,
  `ciede2000 / psnr_hvs == NaN`, `vmaf ~= 99`); parquet carries
  zero training signal. `detect_fr_corpus_misuse(meta_by_clip)` returns
  `{misuse_detected: bool, ref_count, dis_count, content_groups_with_both,
  example}`; `main()` exits 2 before spawning any worker when the loaded
  sidecar carries FR signature. Use `ai/scripts/chug_extract_features.py`
  for FR corpora — it pairs each distorted row with its matching
  reference. `--allow-fr-from-nr` opt-in flag is reserved for genuine
  identity-pair studies on FR corpus; do NOT default-on it in any
  script or recipe. Guard runs on **loaded** `jsonl_meta` dict
  (after `_load_jsonl_metadata` filters raw sidecar), so
  `_load_jsonl_metadata`'s keep-list MUST preserve `chug_ref` and
  `chug_content_name`. Pinned by 3 unit tests
  (`test_detect_fr_corpus_misuse_*`) in `ai/tests/test_extract_k150k_features.py`.

- **`extract_k150k_features.py` must fail loud, never write silent
  garbage row.** Two corruption paths were closed (T-K150K-TRAINING-DATA-
  INTEGRITY-2026-06-20) and invariants must survive rebases:
  (1) `_process_clip` **raises** on empty frame list — never let   all-`NaN` aggregate row reach corpus + `_append_done` (it would be
  dropped with no retry). (2) MOS-label join tolerates   filename↔`video_name` extension mismatch via `mp4.stem` fallback and
  is guarded by up-front coverage check that hard-fails zero-match
  case before any multi-day GPU extraction starts. Do not "simplify"   lookup back to single `mos_map.get(clip_name, NaN)` — that is bug.
  Staging→`.done` write order is deliberately staging-first (crash
  leaves clip re-processable; final parquet dedups by `clip_name`,
  `keep="last"`); do not reorder it.
