# MCP tool reference

Per-tool request / response schemas and error semantics for
[`vmaf-mcp`](index.md). Source of truth: the `list_tools()` handler in
[mcp-server/vmaf-mcp/src/vmaf_mcp/server.py](../../mcp-server/vmaf-mcp/src/vmaf_mcp/server.py).

Every tool returns a single `TextContent` message whose body is a JSON
document. On error the body has shape `{"error": "<string>"}`, so clients
can `json.loads()` unconditionally and branch on the presence of
`error`.

## `vmaf_score`

Score one `(ref, dis)` YUV pair and return the full VMAF JSON report.

### Input schema

| Field       | Type                                   | Required | Default                 | Notes                                          |
|-------------|----------------------------------------|----------|-------------------------|------------------------------------------------|
| `ref`       | string (path)                          | yes      | —                       | Reference YUV; must be under an allowed root   |
| `dis`       | string (path)                          | yes      | —                       | Distorted YUV; same allowlist                  |
| `width`     | integer `≥ 1`                          | yes      | —                       | Frame width in pixels                          |
| `height`    | integer `≥ 1`                          | yes      | —                       | Frame height in pixels                         |
| `pixfmt`    | `"420" \| "422" \| "444"`              | yes      | —                       | YUV chroma subsampling                         |
| `bitdepth`  | `8 \| 10 \| 12 \| 16`                  | yes      | —                       | Bit depth of both YUV files                    |
| `model`     | string                                 | no       | `"version=vmaf_v0.6.1"` | Any `--model` grammar from the CLI             |
| `backend`   | `"auto" \| "cpu" \| "cuda" \| "sycl" \| "vulkan" \| "hip" \| "metal"` | no       | `"auto"`                | Backend selection; `auto` lets vmaf pick. Requesting a backend the local binary does not advertise raises (no silent fallback — ADR-0495). |
| `precision` | string                                 | no       | `"17"`                  | Passed straight to `--precision` (see below)   |

### Behaviour

The server exec's the local `vmaf` binary with, effectively:

```bash
vmaf -r <ref> -d <dis> --width <w> --height <h> -p <pixfmt> -b <bitdepth> \
     -m <model> --precision <precision> -q --json -o <tmp>
# plus per-backend flags:
#   backend=cpu  → --no_cuda --no_sycl
#   backend=cuda → --no_sycl
#   backend=sycl → --no_cuda
```

The JSON written by vmaf is parsed and returned with two extra
fields injected by the MCP layer (ADR-0495):

- `backend_requested` — verbatim echo of the caller's `backend` arg.
- `backend_used` — what actually ran. For an explicit `backend` arg
  this equals the requested value (the wrapper refuses to silently
  fall back); for `backend="auto"` it's a best-effort label
  inferred from the JSON's per-backend key-count signature
  (`"cpu"` / `"gpu"` / `"vulkan"`).

When the local `vmaf` binary does not advertise the requested
backend, the wrapper raises rather than running CPU silently
(the bug-1 pattern from the 2026-05-17 probe). Use
`backend="auto"` to opt back into vmaf's own probe.

The wrapper additionally emits a `mismatched_model_warning` field
when the model's intended resolution preset disagrees with the
source frame size — e.g. `version=vmaf_4k_v0.6.1` on a 576×324
source saturates at 100 on every frame and the warning surfaces
the foot-gun. Bespoke ONNX models with no known resolution
preset are silent (no false positives). See
[usage/cli.md](../usage/cli.md#output) for the rest of the report
schema; the temp file is always unlinked, even on error.

> **`precision` default `"17"`.** The MCP server explicitly passes
> `--precision 17` (`%.17g`, IEEE-754 round-trip lossless) so MCP
> consumers always get scores that re-parse to the exact same double.
> The underlying `vmaf` CLI default is `%.6f` for Netflix-compat per
> [ADR-0119](../adr/0119-cli-precision-default-revert.md); MCP overrides
> it because programmatic consumers (re-parsing the JSON) want the
> lossless form by default. Pass `"6"` (or `"legacy"`) to match the CLI
> default exactly.

### Example call

```json
{
  "method": "tools/call",
  "params": {
    "name": "vmaf_score",
    "arguments": {
      "ref":      "python/test/resource/yuv/src01_hrc00_576x324.yuv",
      "dis":      "python/test/resource/yuv/src01_hrc01_576x324.yuv",
      "width":    576,
      "height":   324,
      "pixfmt":   "420",
      "bitdepth": 8,
      "backend":  "cpu"
    }
  }
}
```

Response body (abridged):

```json
{
  "version": "3.x.y-lusoris.N",
  "pooled_metrics": { "vmaf": { "mean": 76.668905, "...": "..." } },
  "frames": [ { "frameNum": 0, "metrics": { "vmaf": 78.8263, "...": "..." } } ]
}
```

### Errors

- Path not under an allowlisted root → `{"error": "path ... not under an allowlisted root; set VMAF_MCP_ALLOW to extend."}`.
- Path does not exist → `{"error": "<abs-path>"}` from `FileNotFoundError`.
- vmaf binary missing → `{"error": "vmaf binary not found at ...; Build first: meson compile -C build."}`.
- Non-zero vmaf exit → `{"error": "vmaf exited <code>: <stderr>"}`.
- Caller-requested backend not advertised by the local binary →
  `{"error": "backend 'cuda' requested but the local vmaf binary
  does not advertise it (available: ['cpu']); refusing to fall back
  silently. Pass backend='auto' to let vmaf pick, or rebuild with
  the requested backend enabled."}` (ADR-0495).

## `list_models`

Walk `model/` (recursively) and list every `.json`, `.pkl`, or `.onnx`
file shipped with the build.

### Input schema — no arguments.

### Response body

```json
{
  "models": [
    {
      "name": "vmaf_v0.6.1",
      "path": "model/vmaf_v0.6.1.json",
      "format": "json",
      "size_bytes": 9128
    },
    {
      "name": "lpips_sq_small",
      "path": "model/tiny/lpips_sq_small.onnx",
      "format": "onnx",
      "size_bytes": 4873216
    }
  ]
}
```

`name` is the file stem (no extension). Use it with `vmaf_score`'s
`model` field as `"version=<name>"` for built-in `.json` models or as a
plain path for custom `.pkl` / `.onnx`.

### Errors — none in the normal case (an empty `model/` returns `{"models": []}`).

## `list_backends`

Probe the local vmaf binary and report which runtime backends it was
built with.

### Input schema — no arguments.

### Response body

```json
{
  "cpu":  true,
  "cuda": true,
  "sycl": false,
  "hip":  false
}
```

The server runs `vmaf --version` with a 5-second timeout and grep's the
output; `cpu` is reported `true` whenever the binary exists.

### Errors

- If the vmaf binary is missing, every flag is `false` — no error is
  raised. Call `list_backends` before other tools to test whether the
  build is usable.

## `run_benchmark`

Run the full multi-fixture benchmark suite (`testdata/bench_all.sh`) against all
available backends — CPU, CUDA, SYCL, and Vulkan — on three canonical YUV fixture
pairs built into the harness:

1. **576×324, 48 frames, 8-bit** — the Netflix golden pair `src01_hrc00 / src01_hrc01`
2. **1920×1080, 5 frames, 8-bit** — the 5-frame 1080p pair
3. **3840×2160, 200 frames, 8-bit** — the 4K BBB excerpt (`testdata/bbb/`)

For each fixture the harness scores all four backends, prints per-backend VMAF means
and wall times, and prints a comparison table showing max per-frame diff between
CPU and each GPU backend. See [usage/bench.md](../usage/bench.md) for more detail.

> **This tool does not accept per-call `ref`/`dis` arguments.** Per-pair scoring is
> the job of `vmaf_score`. `bench_all.sh` is a fixed-fixture harness. (ADR-0517)

> **Protocol note**: `run_benchmark` runs the full 4K test which takes 30–60 seconds
> on a modern GPU. Real MCP clients hold the connection open. The heredoc test pattern
> (`docker exec -i ... vmaf-mcp << EOF ... EOF`) causes the server to shut down on
> stdin EOF before the benchmark completes. Use a persistent pipe (`sleep 120 |`)
> when testing from the command line. See [Finding 9 in the E2E test matrix](../../.workingdir/bbb_reports/E2E_TEST_MATRIX_v9.md).

### Input schema

Takes no arguments.

```json
{}
```

### Response body

```json
{
  "exit_code": 0,
  "stdout": "=========================================\nTest 1: Official 576x324 (48 frames, 8-bit)\n...",
  "stderr": ""
}
```

The `stdout` field contains the full human-readable benchmark output. Per-backend JSON
result files are written to `/tmp/vmaf-bench-<pid>/` (or to `VMAF_BENCH_OUTDIR` if set).

### Errors

- `testdata/bench_all.sh` missing → raises `FileNotFoundError` with path.
- Non-zero `exit_code` is not itself an error field — both stdout and stderr are
  always returned so the caller can diagnose partial failures.
- Non-zero exit + empty stdout + empty stderr → `error` key is added with a
  root-cause shortlist and a `bash -x` re-run hint. Common causes: missing vmaf
  binary or missing fixture YUVs under `testdata/bbb/`.
- Unavailable backends (Vulkan without ICD, HIP scaffold-only) produce a `SKIP`
  line in stdout and do not abort the harness.

## `eval_model_on_split`

Load an ONNX tiny-AI regressor, run it against a parquet feature cache,
filter to a deterministic `train` / `val` / `test` split (keyed by the
`key` column via SHA-256 bucketing — same scheme as `vmaf_train`), and
report correlations against the `mos` target.

Requires the optional `eval` extra:

```bash
pip install -e 'mcp-server/vmaf-mcp[eval]'
```

which pulls in `numpy`, `pandas`, `scipy`, and `onnxruntime`.

### Input schema

| Field        | Type                                                         | Required | Default      |
|--------------|--------------------------------------------------------------|----------|--------------|
| `model`      | string (path to `.onnx`)                                     | yes      | —            |
| `features`   | string (path to `.parquet`)                                  | yes      | —            |
| `split`      | `"train" \| "val" \| "test" \| "all"`                        | no       | `"test"`     |
| `input_name` | string — the ONNX graph's input-tensor name                  | no       | `"features"` |

### Feature-column contract

The parquet must contain the column `mos` (ground-truth subjective
score). For the input tensor, the server picks whichever of these
columns are present:

- `adm2`
- `vif_scale0`, `vif_scale1`, `vif_scale2`, `vif_scale3`
- `motion2`

At least one must be present, in that order. The ONNX model must
accept a `float32` tensor of shape `[N, K]` where `K` is the number
of columns found.

### Response body

```json
{
  "model":    "/home/you/dev/vmaf/model/tiny/lpips_sq_small.onnx",
  "features": "/home/you/feature-cache/netflix-public.parquet",
  "split":    "test",
  "n":        137,
  "plcc":     0.9743,
  "srocc":    0.9612,
  "rmse":     3.214,
  "columns":  ["adm2", "vif_scale0", "vif_scale1", "vif_scale2", "vif_scale3", "motion2"]
}
```

### Errors

- Bad split name → `{"error": "split must be one of ('train', 'val', 'test', 'all'); got 'foo'"}`.
- Missing `mos` column → `{"error": "<path> has no 'mos' column — can't score correlations"}`.
- Missing all feature columns → `{"error": "... has none of the expected feature columns ..."}`.
- Fewer than 2 samples in the chosen split → `{"error": "split 'test' has N samples — need ≥2 to compute correlations"}`.
- Model output shape ≠ target shape → `{"error": "model output shape ... does not match target shape ..."}`.
- `eval` extra not installed → `{"error": "eval_model_on_split requires the 'eval' extra: pip install 'vmaf-mcp[eval]'"}`.

## `compare_models`

Rank several ONNX models on the same parquet split by descending PLCC.
Models that fail to load or score are collected under `errors` instead
of aborting the whole call — so the agent can surface partial results.

### Input schema

| Field        | Type                                                         | Required | Default      |
|--------------|--------------------------------------------------------------|----------|--------------|
| `models`     | array of string (paths to `.onnx`), `minItems: 1`            | yes      | —            |
| `features`   | string (path to `.parquet`)                                  | yes      | —            |
| `split`      | `"train" \| "val" \| "test" \| "all"`                        | no       | `"test"`     |
| `input_name` | string                                                       | no       | `"features"` |

### Response body

```json
{
  "ranked": [
    { "model": "/.../baseline_v3.onnx",  "plcc": 0.9743, "srocc": 0.9612, "rmse": 3.21, "n": 137, "split": "test", "columns": [ "..." ] },
    { "model": "/.../baseline_v2.onnx",  "plcc": 0.9611, "srocc": 0.9503, "rmse": 3.80, "n": 137, "split": "test", "columns": [ "..." ] }
  ],
  "errors": [
    { "model": "/.../broken.onnx", "error": "model output shape (137, 2) does not match target shape (137,)" }
  ]
}
```

`ranked` is sorted descending by `plcc`. `errors` preserves the input
order for models that failed, with the raised exception serialised as
a string.

### Errors

- Empty or non-list `models` → `{"error": "'models' must be a non-empty list of paths"}`.
- Individual model failures show up under the `errors` array, not as a
  top-level error.

## `describe_worst_frames`

Score a `(ref, dis)` pair, pick the N frames with lowest VMAF, extract
each as PNG via `ffmpeg`, and run a vision-language model
(SmolVLM → Moondream2 fallback) to describe the visible artefacts.
Falls back to a metadata-only output when the `vlm` extras aren't
installed — useful as a debugging affordance for an LLM agent that
wants narrative context for low-quality regions. Added in
[ADR-0172](../adr/0172-mcp-describe-worst-frames.md) (T6-6).

### Input schema

| Field      | Type                                                | Required | Default                  |
|------------|-----------------------------------------------------|----------|--------------------------|
| `ref`      | string (path to reference YUV)                      | yes      | —                        |
| `dis`      | string (path to distorted YUV)                      | yes      | —                        |
| `width`    | integer                                             | yes      | —                        |
| `height`   | integer                                             | yes      | —                        |
| `pixfmt`   | `"420"` / `"422"` / `"444"`                         | yes      | —                        |
| `bitdepth` | 8 / 10 / 12 / 16                                    | yes      | —                        |
| `model`    | string                                              | no       | `"version=vmaf_v0.6.1"`  |
| `backend`  | `"auto"` / `"cpu"` / `"cuda"` / `"sycl"` / `"vulkan"` / `"hip"` / `"metal"` | no       | `"auto"`                 |
| `n`        | integer in `[1, 32]`                                | no       | `5`                      |

### Behaviour

1. Run `vmaf_score` to populate per-frame VMAF.
2. Pick the `n` frames with smallest VMAF.
3. For each picked frame, run `ffmpeg -f rawvideo` with
   `select='eq(n,<idx>)'` to emit a single PNG.
4. Pass the PNG to the cached VLM pipeline. The pipeline is loaded
   lazily on first call:
   - Try `HuggingFaceTB/SmolVLM-Instruct` (~2 GB).
   - Fall back to `vikhyatk/moondream2` (~2 GB).
   - If neither loads (or `transformers` isn't importable), every
     frame's `description` carries
     `"(VLM unavailable — install with pip install vmaf-mcp[vlm])"`.
5. Return frame metadata + descriptions.

The PNGs are written under `/tmp/vmaf-mcp-worst-<pid>/`. They aren't
auto-deleted — callers can fetch them at the returned `png` paths
during the lifetime of the process.

### Response body

```json
{
  "model_id": "HuggingFaceTB/SmolVLM-Instruct",
  "frames": [
    {
      "frame_index": 12,
      "vmaf": 38.4,
      "png": "/tmp/vmaf-mcp-worst-12345/frame_000012.png",
      "description": "Heavy DCT blocking on the face and ringing along the chin contour."
    }
  ]
}
```

`model_id` is `null` when the metadata-only fallback path fired.

### Errors

- `ffmpeg` not on PATH → `{"error": "ffmpeg not on PATH; install ffmpeg to use describe_worst_frames"}`.
- Unsupported `pixfmt`/`bitdepth` combo → `{"error": "unsupported pixfmt/bitdepth combo: ..."}`.
- VMAF subprocess failure → bubbles up the underlying `vmaf_score` error.
- VLM inference exception per-frame → the frame's `description`
  carries the exception string; other frames still proceed.

## Cross-tool error conventions

| Situation                               | Shape                                                   |
|-----------------------------------------|---------------------------------------------------------|
| Unknown tool name                       | `{"error": "unknown tool: <name>"}`                     |
| Path outside allowlist                  | `{"error": "path ... not under an allowlisted root"}`   |
| Path does not exist                     | `{"error": "<resolved-abs-path>"}`                      |
| Subprocess non-zero (vmaf_score only)   | `{"error": "vmaf exited <rc>: <stderr>"}`               |
| Missing optional extras                 | `{"error": "... requires the 'eval' extra: ..."}`       |

All exceptions raised inside a tool handler are caught and serialised
into the `error` shape above — the JSON-RPC channel itself never
returns a non-200.

## Related

- [MCP server overview](index.md) — install, security model, env vars.
- [CLI reference](../usage/cli.md) — the CLI that `vmaf_score` wraps.
- [`vmaf_bench`](../usage/bench.md) — what `run_benchmark` drives.
- [Tiny-AI inference](../ai/inference.md) — what
  `eval_model_on_split` / `compare_models` are scoring.
- [ADR-0100](../adr/0100-project-wide-doc-substance-rule.md).
