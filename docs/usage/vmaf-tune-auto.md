<!-- markdownlint-disable MD060 -->
# `vmaf-tune auto` — one command from source to a planned encode

`vmaf-tune auto` takes a source, a target VMAF and a bitrate budget, and
returns a JSON plan that names the codec, CRF and expected quality of the next
encode. With `--execute` it then runs that encode and scores it. It composes the
other subcommands (`corpus`, `recommend`, `predict`, `tune-per-shot`,
`recommend-saliency`, `ladder`, `compare`) and the orthogonal modes (HDR
auto-detect, sample clip, resolution-aware scoring) into one deterministic
decision tree (ADR-0325, [ADR-0397](../adr/0397-vmaf-tune-phase-f-auto.md)). The
tool overview is in
[`vmaf-tune.md`](vmaf-tune.md).

## Quick start

```shell
vmaf-tune auto \
    --src reference.mp4 \
    --target-vmaf 93 \
    --max-budget-bitrate 5000 \
    --allow-codecs libx264,libx265 \
    --output plan.json
```

Add `--smoke` to exercise the whole composition with mocked sub-phases and no
ffmpeg or ONNX. Add `--execute` to also run the selected encode, see
[Execute mode](#execute-mode).

Exit codes: `0` for a plan-only run, whatever the plan holds; `1` when
`--execute` ran at least one cell and none scored; `2` for an empty
`--allow-codecs` or a planner error.

## Flags

| Flag | Default | Meaning |
|---|---|---|
| `--src PATH` | none (required) | Reference video: raw YUV or any FFmpeg-readable container. |
| `--target-vmaf F` | `93.0` | Target pooled-mean VMAF. |
| `--max-budget-bitrate K` | `8000.0` | Upper bound on the picked rendition's bitrate, in kbps. |
| `--allow-codecs LIST` | `libx264` | Comma-separated codecs the tree may pick from. A single entry short-circuits the compare shortlist. |
| `--codec NAME` | none | Pins the codec, overriding the `--allow-codecs` ranking. Also short-circuits the shortlist. |
| `--sample-clip-seconds N` | `0.0` | Propagates this clip length to the internal sweeps instead of re-deciding it per stage (ADR-0301). `0` means the full source. |
| `--smoke` | off | Composition end to end with mocked sub-phases. |
| `--output PATH` | stdout | Write the JSON plan here. |
| `--execute` | off | Run real encodes and scores for the selected cell. Plan-only without it. |
| `--runs-dir PATH` | `runs` | Destination of the encodes and `tune_results.jsonl`. |
| `--execute-all` | off | Run every plan cell instead of only the selected winner. |

## How planning works

The non-smoke path probes source geometry, duration and HDR metadata with the
same ffprobe and HDR helpers that the corpus path uses. A failed probe degrades
to conservative 1920x1080 SDR defaults, so the planner can still emit a plan
and show which later stages need real evidence.

For each non-smoke cell, `auto` feeds the probed metadata into the existing
`Predictor`, picks a codec-specific CRF for
`metadata.effective_predictor_target_vmaf`, and records the predicted
`estimated_vmaf` and `estimated_bitrate_kbps`. These are planner estimates, not
measured results, until `--execute` scores the chosen cell.

The per-cell `prediction_source` key tells the two paths apart: `"predictor"`
is the production estimate and `"smoke-placeholder"` is the `--smoke`
placeholder. The plan's `metadata.short_circuits` records which short-circuits
fired, so post-hoc analysis can measure each one's speedup.

For HDR sources every cell records the codec-specific `hdr_args` from
`vmaftune.hdr.hdr_codec_args(codec, info)`:

| Codec | HDR arguments recorded |
|---|---|
| x264 | Container-level `-color_*` flags only. |
| x265 | `-x265-params` SEI signalling. |
| SVT-AV1 | `-svtav1-params`. |
| any, SDR source | An empty list, after the `sdr-skip` short-circuit fires. |

`auto` never dispatches [`fast`](vmaf-tune-fast-path.md) from inside its tree.
`fast` is a different operator surface (proxy plus Bayesian search over a
single codec) and stays a sibling of `auto`.

## Short-circuits

Ten guarded fast paths skip a stage when one trigger condition holds. Each
predicate is a `_should_short_circuit_<N>` helper in
`tools/vmaf-tune/src/vmaftune/auto.py`, so it can be unit-tested alone.

| # | Identifier | Trigger | Skips |
|---|---|---|---|
| 1 | `ladder-single-rung` | `meta.height < 2160` | Multi-rung ABR ladder evaluation (ADR-0289, ADR-0295). |
| 2 | `codec-pinned` | `--codec` set, or `--allow-codecs` resolves to one entry | The `compare.shortlist` stage. |
| 3 | `predictor-gospel` | `predict.crf_for_target` returns `GOSPEL` (ADR-0306) | The `recommend.coarse_to_fine` fallback for that cell. |
| 4 | `skip-saliency` | `meta.content_class` is not `animation` or `screen_content` (so photographic and live action skip) | The `recommend_saliency.maybe_apply` stage (ADR-0293). |
| 5 | `sdr-skip` | `not meta.is_hdr` (ADR-0300 detector) | The HDR resolution and model-selection branch. |
| 6 | `sample-clip-propagate` | `--sample-clip-seconds > 0` | Re-deciding the clip length per stage. The value propagates verbatim (ADR-0301). |
| 7 | `skip-per-shot` | `duration < 5 min` and `shot_variance < 0.15` | The `tune_per_shot.refine` pass (ADR-0392). |
| 8 | `low-complexity` | `meta.complexity_score < 200` kbps (probe-encode bitrate) | The `recommend.coarse_to_fine` sweep: the predictor's estimate is already tight on simple content. `0.0` or `NaN` does not fire, because no probe has run. |
| 9 | `baseline-meets-target` | `meta.baseline_vmaf >= target_vmaf` | The full predictor sweep: the default-CRF encode already meets the target. `0.0` or `NaN` does not fire, because no baseline is scored yet. |
| 10 | `no-two-pass` | `adapter.supports_two_pass == False` (ADR-0333, ADR-0546) | The two-pass calibration stage. |

Notes on the table:

- **Short-circuit 10.** Hardware encoders (`*_nvenc`, `*_amf`, `*_qsv`,
  `*_videotoolbox`) and `libsvtav1` (CRF-mode multi-pass prohibition) fire it.
  `libx264`, `libx265`, `libvpx-vp9`, `libaom-av1` and `libvvenc` set
  `supports_two_pass = True`.
- **Placeholder thresholds.** The 5 minutes and 0.15 of #7 are
  `PHASE_D_DURATION_GATE_S` and `PHASE_D_SHOT_VARIANCE_GATE` at the top of
  `auto.py`, awaiting an empirical fit. The 200 kbps of #8 is
  `LOW_COMPLEXITY_PROBE_BITRATE_THRESHOLD_KBPS` in the same file.
- **Order is a contract.** The order of `SHORT_CIRCUIT_PREDICATES` is part of
  the public contract. Tests assert that an earlier predicate does not shadow
  a later one whose result would differ. Add new short-circuits at the end and
  never reorder.

## Winner selection

`auto` ends the planning pass by selecting one estimated cell. The chosen cell
has `"selected": true` in `cells[]` and every other cell has `"selected":
false`. The same decision is copied to `metadata.winner`, so scripts can read
one stable object without scanning the array.

| Status | Meaning |
|---|---|
| `budget_and_quality_met` | At least one cell met `--target-vmaf` and `--max-budget-bitrate`. The lowest estimated bitrate wins. |
| `quality_met_budget_exceeded` | No cell was inside the budget, but at least one met the quality target. The smallest budget overage wins. |
| `target_unmet` | No cell met the quality target. The closest estimated VMAF miss wins, so the caller still gets a concrete next encode. |
| `no_eligible_cells` | No cell had a finite `estimated_vmaf` and `estimated_bitrate_kbps`. This is an input or planner evidence failure. |

`metadata.winner` records `cell_index`, `rung`, `codec`, `crf`,
`estimated_vmaf`, `estimated_bitrate_kbps`, `quality_margin` and
`budget_margin_kbps`. It is still a planning result: the winner is the next
encode target, not a substitute for the final encode and score pass.

## Execute mode

`--execute` (ADR-0454) drives real FFmpeg encodes and libvmaf scores for the
selected cell after the planning pass:

```shell
vmaf-tune auto \
    --src reference.mp4 \
    --target-vmaf 93 \
    --max-budget-bitrate 5000 \
    --allow-codecs libx264,libx265 \
    --output plan.json \
    --execute \
    --runs-dir runs/
```

- **Results.** One row per executed cell is appended to
  `<runs-dir>/tune_results.jsonl`. A row merges the cell metadata (codec,
  preset, CRF, estimated VMAF and bitrate) with the encode outcome (size,
  encode time, FFmpeg version) and the score outcome (measured VMAF,
  per-feature means and standard deviations, vmaf binary version).
- **Appending.** The file grows on every run, so partial runs and incremental
  re-runs never overwrite earlier results.
- **Source type.** The CLI executes with the container-source defaults of
  `run_plan()`. Use a container as `--src`; the Python API takes explicit
  geometry for raw YUV.
- **Exit status.** `1` when at least one cell ran and none scored (encode
  failures, missing `vmaf` binary and the like), otherwise `0`.

### Per-shot and saliency execution (Python API)

Two further executors exist in `vmaftune.executor`. The CLI does not call
them; use them from Python.

`run_plan_per_shot` (ADR-0468) splits the source into shots with
`vmaf-perShot` ([ADR-0223](../adr/0223-transnet-v2-shot-detector.md)) and
scores each segment on its own. It appends to
`<runs-dir>/tune_results_per_shot.jsonl`. Each top-level row carries
`shot_count` and `weighted_vmaf`, the frame-length-weighted mean of the
per-shot scores. Without `vmaf-perShot` the call falls back to one shot over
the whole clip, and `shot_count == 1` signals that.

```python
from pathlib import Path

from vmaftune.executor import run_plan_per_shot

per_shot_results = run_plan_per_shot(
    plan, src=Path("reference.mp4"), out_dir=Path("runs/"),
    vmaf_model="vmaf_v1.0.16_3d0h",
)
for r in per_shot_results:
    print(f"cell {r.row['cell_index']}: "
          f"{r.row['shot_count']} shots, "
          f"weighted VMAF={r.weighted_vmaf:.2f}")
```

`run_plan_saliency` (ADR-0468) encodes through `saliency_aware_encode` (see
[`vmaf-tune-saliency-aware.md`](vmaf-tune-saliency-aware.md)) before scoring.
It appends to `<runs-dir>/tune_results_saliency.jsonl`.
`saliency_available` is `True` when the ONNX model ran. `False` means the
encoder fell back to a plain encode because the model file is missing or
onnxruntime is not installed. The encode and score proceed either way.

```python
from pathlib import Path

from vmaftune.executor import run_plan_saliency

sal_results = run_plan_saliency(
    plan, src=Path("reference.mp4"), out_dir=Path("runs/"),
    saliency_model_path=Path("model/tiny/saliency_student_v1.onnx"),
    duration_frames=total_frame_count,
)
for r in sal_results:
    print(f"cell {r.row['cell_index']}: "
          f"saliency_available={r.saliency_available}, "
          f"VMAF={r.row['vmaf_score']}")
```

## Confidence-aware fallbacks

Short-circuit #3 treats the predictor's verdict as a binary `GOSPEL` or
`FALL_BACK` gate. The confidence-aware layer makes that gate continuous. It
reads the conformal interval half-width from
`Predictor.predict_vmaf_with_uncertainty`
([ADR-0393](../adr/0393-fr-regressor-v2-probabilistic.md)) and compares it
with two width gates:

| Interval width | Outcome | Effect |
|---|---|---|
| `width <= tight_interval_max_width` | `SKIP_ESCALATION` | The predictor is confident. Trust the point estimate even when the native verdict said `FALL_BACK`. |
| between the two gates | `RECOMMEND_ESCALATION` on `FALL_BACK` or unknown, `SKIP_ESCALATION` on `GOSPEL` or `LIKELY` | Defer to the native verdict. |
| `width >= wide_interval_min_width` | `FORCE_ESCALATION` | The predictor is uncertain. Escalate to `recommend.coarse_to_fine` even when the verdict said `GOSPEL`. |

The two thresholds come from the conformal-VQA calibration pipeline, which
ships a JSON sidecar with the keys `tight_interval_max_width` and
`wide_interval_min_width`. The loader honours per-corpus overrides. When no
sidecar is found it falls back to the Research-0067 floor of `2.0` and `5.0`
VMAF and logs a one-line warning. The floor is documented behaviour, not a
magic constant.

Decisions are recorded per cell in
`plan.metadata.confidence_aware_escalations[]`
(keys `rung`, `codec`, `verdict`, `interval_width`, `decision`). Each cell in
`plan.cells[]` also carries `confidence_decision` and `interval_width`, so
consumers need not cross-reference the metadata array index.

The thresholds are calibration outputs. If a sidecar value triggers surprising
escalations on real data, recalibrate; do not loosen the gate here. The pure
helper `_confidence_aware_escalation(verdict, interval_width, thresholds)` in
`auto.py` is exposed for unit tests and for downstream tools such as the MCP
server's `auto` proxy and the CI corpus collector.

## Per-content-type recipes

A recipe is a small override dict that `auto` applies **before** the
short-circuits evaluate, so a recipe can, for example, arm the single-rung
ladder. The classifier (`per_shot.detect_shots` plus the fork-local
content-class heuristics) tags a source as `animation`, `screen_content`,
`live_action_hdr` or `ugc`. Any other `meta.content_class` uses the empty
`default` recipe.

### Override keys

| Key | Type | Effect |
|---|---|---|
| `tight_interval_max_width` | float | Narrows or widens the conformal-tight gate of the previous section. |
| `force_single_rung` | bool | Arms short-circuit #1 (`ladder-single-rung`) even on sources of 2160p or more. |
| `saliency_intensity` | str | Passed to the saliency stage when it is not skipped: `default`, `aggressive` or `very_aggressive`. |
| `target_vmaf_offset` | float | Additive offset applied to the *predictor's* effective target. |

!!! note "The input target is never shifted"
    A recipe moves only the predictor's target and the width gate. The input
    `--target-vmaf`, which downstream consumers treat as the contract, is
    recorded unchanged in `plan.metadata.target_vmaf`. The shifted value lives
    separately in `plan.metadata.effective_predictor_target_vmaf`.

### Shipped recipes

The values are the calibrated thresholds in
`ai/data/phase_f_recipes_calibrated.json`, produced by
`ai/scripts/calibrate_phase_f_recipes.py` against the K150K corpus.

| Class | `tight_interval_max_width` | `force_single_rung` | `saliency_intensity` | `target_vmaf_offset` | Source |
|---|---|---|---|---|---|
| `animation` | `1.75` | `true` | `aggressive` | `+2.0` | proxy (UGC-anchored) |
| `screen_content` | unset | unset | `very_aggressive` | `+1.0` | proxy (UGC-anchored) |
| `live_action_hdr` | `1.4` | unset | default | `0.0` | proxy (UGC-anchored) |
| `ugc` | `3.5` | `false` | `default` | `+1.5` | corpus (K150K) |
| `default` | unset | unset | default | `0.0` | n/a |

K150K is a UGC-only corpus without a per-source `content_class` column, so
only the `ugc` row is corpus-derived. The other three rows are documented
absolute offsets ("proxy") anchored on the UGC baseline, until a
class-labelled subset exists. The `recipes.<class>._provenance` entry of the
JSON records which source each row came from.

The UGC `target_vmaf_offset` came out positive (`+1.5`) because the corpus MOS
distribution has a heavier upper tail than lower tail. The calibration script
clamps every offset to the documented envelope of `-2.0` to `+2.0`, so a
pathological corpus cannot push the predictor target outside the range the
planner has been exercised against.

### Why each recipe looks the way it does

- **Animation:** predictor residuals are tighter on flat colour fields, a
  single-rung ladder is enough, and saliency is more aggressive on cel-line
  edges. Animation compresses better at a given perceptual quality, so the
  predictor aims about 2 VMAF higher.
- **Screen content:** the split-frame structure (low-entropy background plus
  high-detail text and icons) benefits from `very_aggressive` saliency, which
  raises QP on the background and keeps text near-lossless. The predictor
  target moves up by 1.
- **Live-action HDR:** the HDR pipeline already runs
  ([ADR-0300](../adr/0300-vmaf-tune-hdr-aware.md)). The tight gate narrows to
  `1.4` because a wide predictor interval on HDR is more suspect than on SDR;
  the predictor was trained largely on SDR
  ([ADR-0393](../adr/0393-fr-regressor-v2-probabilistic.md)).
- **UGC:** user-generated content has noisier upstream encodes, inconsistent
  grading and resolution mismatches, so predictor uncertainty is the baseline.
  Widening the tight gate to `3.5` avoids flagging UGC cells for escalation
  only because their interval is wider than a Netflix-grade reference. The
  K150K fit nudges the target up by 1.5.

### Where recipes show up in the plan

- `plan.metadata.recipe_applied`: `animation`, `screen_content`,
  `live_action_hdr`, `ugc` or `default`.
- `plan.metadata.recipe_overrides`: the override dict.
- Each cell in `plan.cells[]`: the resolved `saliency_intensity` and
  `effective_predictor_target_vmaf`.

`auto.py` loads the calibrated JSON at import (`_load_calibrated_recipes`). If
the file is missing or malformed, the F.4 placeholder constants in
`_F4_PLACEHOLDER_RECIPES` apply instead. The generated JSON carries
[ADR-0661](../adr/0661-ai-run-manifest-provenance.md) `run_provenance`: the
calibration script, its argv, the source corpus JSONL, the row cap and the
output target. To recalibrate, for instance with a class-labelled corpus:

```shell
python ai/scripts/calibrate_phase_f_recipes.py \
    --corpus .corpus/konvid-150k/konvid_150k.jsonl \
    --out ai/data/phase_f_recipes_calibrated.json
```

Two pure helpers in `auto.py` resolve recipes. `_apply_recipe_override(meta,
plan_state, thresholds)` returns a `(recipe_class, recipe,
effective_thresholds)` triple, and `get_recipe_for_class(content_class)`
returns a fresh override dict for any of the five class strings. The table
`_CONTENT_RECIPE_TABLE` holds factory callables, so each call returns a dict
that callers may mutate freely.

## History

- **F.1** shipped the sequential composition of the per-phase subcommands.
- **F.2** added the short-circuits (ten today).
- **F.3** added the confidence-aware fallbacks.
- **F.4** added the per-content-type recipes.
- **F.5** calibrated the recipe thresholds. The run used 148,543 of an
  expected 153,841 K150K rows (about 96.6 % ingested) on 2026-05-09. A re-run
  on the full corpus is a follow-up.
- Threshold rationale and the per-class proxy-versus-corpus breakdown are in
  [Research-0067, section "F.4 recipe-override placeholders"](../research/0067-vmaf-tune-phase-f-feasibility-2026-05-08.md)
  and in the metadata block of the calibration JSON.

## See also

- [`vmaf-tune.md`](vmaf-tune.md) — the tool overview.
- [`vmaf-tune-fast-path.md`](vmaf-tune-fast-path.md) — the sibling `fast`
  subcommand.
- [`vmaf-tune-compare.md`](vmaf-tune-compare.md),
  [`vmaf-tune-per-shot.md`](vmaf-tune-per-shot.md),
  [`vmaf-tune-saliency-aware.md`](vmaf-tune-saliency-aware.md) and
  [`vmaf-tune-recommend.md`](vmaf-tune-recommend.md) — stages that `auto`
  composes.
- [`vmaf-tune-hdr-and-sampling.md`](vmaf-tune-hdr-and-sampling.md) — HDR
  detection and sample-clip mode.
- [ADR-0397](../adr/0397-vmaf-tune-phase-f-auto.md) — design decision and F.4
  recipes.
- [Research-0067](../research/0067-vmaf-tune-phase-f-feasibility-2026-05-08.md)
  — feasibility study.
