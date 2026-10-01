# AGENTS.md — pkg/tune

Parent: [../../AGENTS.md](../../AGENTS.md). CLI wiring:
[cmd/vmafx-tune/AGENTS.md](../../cmd/vmafx-tune/AGENTS.md).

Go port of `vmaf-tune auto` and `vmaf-tune sidecar`. Python originals in
`tools/vmaf-tune/src/vmaftune/` until Go parity (ADR-0703 §Decision,
ADR-0704 §Consequences); **packages here track Python counterpart.**

| Package | Python counterpart |
| --------- | -------------------- |
| `auto/` | `vmaftune/auto.py` |
| `sidecar/` | `vmaftune/sidecar.py` |
| `executor/` | `vmaftune/executor.py` (`run_plan`), `encode.py`, `score.py` |

Shared layers live **outside** `pkg/tune/` (ADR-1137): `pkg/predictor`
(`vmaftune/predictor.py`), `pkg/codecadapter` (`vmaftune/codec_adapters/`),
`pkg/hdr` (`vmaftune/hdr.py`), `pkg/ffencode` (`vmaftune/encode.py`),
`pkg/pyjson` (CPython `json.dumps` and `vmaftune/jsonio.py`), `pkg/pymath`
(CPython `2.0 ** x`, `math.log10` on libm). `pkg/tune/{hdr,pymath}` shadows
deleted (moved to `pkg/hdr` / `pkg/pymath`). `pkg/tune/{predictor,codec,pyjson}`:
**transitional thin aliases** (type aliases, wrappers; tests absent) kept for
`sidecar/` and `cmd/vmafx-tune/cmd/sidecar.go` (#1187 sidecar parity).
When #1187 lands, repoint imports, delete alias packages. Never add aliases;
never re-grow shadows on rebase.

## Rebase-sensitive invariants

1. **Plan JSON byte-compatible with Python emitter, NaN token included**
   (`auto/auto.go` `EmitPlanJSON`, `pkg/pyjson`). `vmaftune.auto` serialises
   `json.dumps(payload, indent=2, sort_keys=True)`; default `allow_nan=True`
   writes bare token `NaN` for uncalibrated conformal `interval_width` (runs
   without `CellIntervals` seam). Go **cannot** use `encoding/json`; uses
   `pyjson.MarshalIndentSorted` matching CPython: `NaN` / `Infinity` tokens,
   `repr()` mandatory `.0` on integral floats, fixed/exponential switch at
   `decpt <= -4 || decpt > 16`, `ensure_ascii=True` escaping.
   `TestEmitPlanJSONMatchesPython` diffs plans vs Python fixtures.
   Do not switch to `pyjson.MarshalStrict`: breaks downstream consumers.
   `--execute` JSONL rows and sidecar state file use strict `dumps_strict`
   (`MarshalStrict`); asymmetry intentional.

2. **`pkg/pymath` parity layer, not micro-optimisation**
   (`pkg/pymath/exp2.go`, `pkg/pymath/log10.go`). Go `math.Pow` and `math.Log10`
   differ 1 ULP from platform libm CPython; results reach JSON fields:
   `estimated_bitrate_kbps` via `2**((probe_quality − crf)/6)` in
   `auto/auto.go`, `estimated_vmaf` via `pkg/predictor` curve
   `+ d·log10(bitrate)`. Stdlib fails parity fixtures. `Exp2` matches CPython
   across `n/6` family (12,606 vectors). `Log10` correctly rounded; agrees
   glibc ~99.3% random inputs vs stdlib ~72%; residual glibc rounding error,
   documented in package.

3. **Short-circuit order: output contract** (`auto/auto.go`
   `ShortCircuitPredicates`). `plan.metadata.short_circuits` records firing
   order for speedup analysis. Append predicates; never reorder/renumber
   existing 10. Predicates remain pure functions `(SourceMeta, *PlanState)`.

4. **Recipe fires before ladder stage** (`auto/auto.go`, Stage 0 vs Stage 1).
   Recipe sets `force_single_rung`; ladder stage requires it. Moving recipe
   after rung selection drops override on 4K sources. Documented keys
   (`tight_interval_max_width`, `force_single_rung`, `saliency_intensity`,
   `target_vmaf_offset`) survive into `metadata.recipe_overrides`;
   calibrator `_provenance` stripped.

5. **Recipe never widens production gate** (`auto/auto.go`
   `applyRecipeThresholds`). `target_vmaf_offset` shifts *predictor* target,
   never model gate. `wide_interval_min_width` preserved verbatim: recipe
   asking tight gate wider than `wide` capped; preserves `tight <= wide`.

6. **Sidecar feature-vector layout pins persisted weights**
   (`sidecar/sidecar.go` `FeatureVector`, `FeatureDim = 14`). Column order:
   disk ridge weight index. Changing/adding column requires bumping
   `SchemaVersion` to prevent mis-assigned `state.json`. Vector ends at
   `Width`; `Height` absent, matching Python.

7. **Predictor-version mismatch discards fit** (`sidecar/sidecar.go`
   `ModelFromMap`, `Load`). Old predictor fit never replayed against refreshed
   predictor. `Load` returns cold start (not error) for version, schema,
   shape mismatch, corrupt JSON (leaves corrupt file for inspection).
   `stateDoc` decodes `weights` and `a_inv` via `*float64`; JSON `null`
   (written by `Save` for NaN) causes load failure (cold start) like CPython
   `float(None)`, never silent 0.

8. **Host UUID random, not machine-derived** (`sidecar/sidecar.go`
   `GetOrCreateHostUUID`). 128 bits `crypto/rand` at cache root; survives
   predictor upgrades. Never derive from MAC, hostname, `/etc/machine-id`,
   CPUID, or identifying signals (precondition for opt-in upload).

9. **Cold start returns exactly `0.0`** (`sidecar/sidecar.go`
   `PredictCorrection`). 0 weights -> dot product 0; `sidecar.Predictor`
   degenerates to bare predictor until first capture. Epsilon init would
   perturb untrained scores.

10. **Subprocess seam required for testability** (`pkg/hdr.Runner`,
    `executor.Runner`). ffprobe / ffmpeg / vmaf run through injectable runner;
    tests run without binaries. Never inline `exec.Command`. Non-zero exit
    reported via result; `error` reserved for spawn failure.

11. **Probe failure degrades, does not abort** (`auto/auto.go` `ProbeSourceMeta`,
    `pkg/hdr/hdr.go` `Detect`). Missing ffprobe, non-zero exit, unparseable
    output fall back to defaults (1920x1080, duration 0, SDR). HDR detection
    permissive one-way: SDR misclassified as HDR injects PQ signalling into
    gamma-2.4; PQ without BT.2020 treated as SDR.

12. **Executor argv uses `pkg/ffencode`; AMF tail emitted once**
    (`executor/executor.go` `BuildFFmpegCommand`). `EncodeRequest`: type alias
    of `ffencode.Request`; `-ss` / `-t`, `DurationS` fallback, codec-adapter
    slice pinned in `executor_test.go`. Argv builder lenient
    (`(*codecadapter.Adapter).ResolveCodecArgs` passes out-of-vocabulary preset);
    package `codecadapter.ResolveCodecArgs` strict. Plan driver emits `medium`.
    AMF duplicate `-quality / -rc / -qp_i / -qp_p` tail dropped (ADR-1125,
    pkg/codecadapter `AGENTS.md` invariant 3).

13. **Plan cells omit `cell_index` and `preset`** (`executor/executor.go`
    `cellToEncodeRequest`, `makeRow`). Planner omits keys; executor defaults
    index 0, preset `medium`; JSONL records both `null` (Python behaviour).
    Emitting keys changes row shape, requires note in `docs/`.

## Regenerating the parity fixtures

Fixtures `testdata/python_*.json` and ADR-1137 moved fixtures
(`pkg/predictor/testdata/python_predictor.json`,
`pkg/codecadapter/testdata/python_adapters.json`,
`pkg/hdr/testdata/python_hdr.json`, `pkg/pyjson/testdata/float_repr.txt`,
`pkg/pymath` reference vectors) dumped from Python. Regenerate **only** on
coordinated changes; silent regen breaks parity gate. Loader documents schema.
