<!-- markdownlint-disable MD060 -->
# `vmaf-tune --score-backend` — GPU acceleration of the scoring loop

`--score-backend` picks the libvmaf backend (`cuda`, `sycl`, `hip` or `cpu`)
that scores every encoded variant, so the score axis of a sweep runs on a
GPU while the encode axis stays unchanged. The default, `auto`, probes the
host and takes the fastest backend it finds. This page is the reference for
the flag; the tool overview is in [`vmaf-tune.md`](vmaf-tune.md).

## Usage

```shell
vmaf-tune corpus \
    --source ref.yuv --width 1920 --height 1080 \
    --pix-fmt yuv420p --framerate 24 \
    --encoder libx264 --preset medium --crf 22 --crf 28 \
    --score-backend cuda \
    --output corpus.jsonl
```

Every run prints the backend it resolved to on stderr, for example
`vmaf-tune: scoring backend = cuda`. Check that line first when a sweep is
slower than expected.

The same flag is accepted by these subcommands:

| Subcommand | Default |
|---|---|
| `corpus` | `auto` |
| `recommend` | `auto` |
| `tune-per-shot` | `auto` |
| `ladder` | `auto` |
| `fast` | `auto` |
| `prefilter` | `auto` |
| `compare` | unset (resolved like `auto` for the pre-check; the scorer then picks its own default) |

`report` also has a `--score-backend` option, but it only records the value in
the encoder profile and does not score anything.

## Accepted values

| Value | Behaviour |
|---|---|
| `auto` | Probe the host and pick the first usable backend in the order `cuda`, `sycl`, `hip`, `cpu`. Falls back to CPU only because no GPU was found. |
| `cuda` | NVIDIA GPU. Errors out if the local `vmaf` was built without CUDA, or if `nvidia-smi` is missing or reports no device. |
| `sycl` | Intel oneAPI SYCL. Errors out if `vmaf` was built without SYCL, or if `sycl-ls` lists no GPU device. |
| `hip` | AMD ROCm. Errors out if `vmaf` was built without HIP, or if `rocminfo` / `rocm-smi` cannot see an AMD GPU. |
| `cpu` | Force the CPU path. Always available; use it as the reference when chasing a numeric divergence or to match the Netflix golden-data gate. |

!!! note "Vulkan is gone"
    The `vulkan` backend was removed in
    [ADR-0726](../adr/0726-drop-vulkan-backend.md). Argparse rejects
    `--score-backend vulkan` as an invalid choice (exit code 2). Use `hip` for
    AMD GPUs.

## Strict versus automatic selection

- **An explicit value is strict.** `--score-backend cuda` on a host without
  CUDA fails before any encode starts, with a message that lists the backends
  the host does offer (the error is `BackendUnavailableError`). It never falls
  back to CPU, so a "GPU sweep" cannot quietly produce CPU numbers.
- **`auto` walks the chain.** It takes the first backend that the local `vmaf`
  binary advertises in `--help` **and** that a vendor tool confirms on the
  host.

```text
vmaf-tune: backend 'cuda' requested but not available on this host
(available: cpu). Check that the local vmaf binary was built with the
matching backend support and the corresponding runtime/driver is
installed.
```

The strict failure exits with code `2` for the subcommands that resolve the
backend up front. The ADR-0299 guarantee (no silent downgrade) holds for all
four values.

## How the host is probed

`vmaf-tune` reads the `vmaf --help` output to learn which backends the binary
supports. The CLI prints a line of the form
`--backend $name: ...auto|cpu|cuda|sycl|hip`. It then runs one cheap hardware
probe per backend:

| Backend | Probe passes when |
|---|---|
| CUDA | `nvidia-smi -L` returns at least one `GPU` line. |
| SYCL | `sycl-ls` lists at least one `:gpu` device. |
| HIP | `rocminfo` reports a `gfx*` agent, or `rocm-smi` reports a GPU. |

A missing tool means "backend not available", never a hard error. CPU is
always available, even when the help line is missing.

!!! note "Probe order differs from libvmaf"
    The vmaf-tune order (`cuda`, `sycl`, `hip`, `cpu`) is not the libvmaf
    registry order (`sycl`, `cuda`, `hip`, `cpu`). vmaf-tune probes CUDA first
    because `nvidia-smi` is the most reliable availability check
    ([ADR-0667](../adr/0667-vmaf-tune-score-backend-native-priority.md)).
    On a host with both, `auto` can therefore pick a different backend than a
    direct `vmaf --backend auto`, so keep that in mind when comparing timings.

## Performance

The GPU score path is roughly 10 to 30 times faster than the CPU path on
1080p / 24 fps content. CPU VMAF runs at about 1 to 2 fps at 1080p. The flag
does not touch the encode axis: when the encode dominates wall time (for
example `libsvtav1` at `--preset 0`), a GPU score backend barely changes the
total sweep time.

Indicative wall clock for a 60 s 1080p source:

| Score backend | Hardware | Wall clock | Throughput |
|---|---|---|---|
| `cpu` | AVX2 desktop CPU | about 600 to 1200 s | about 1.2 to 2.5 fps |
| `cuda` | RTX 30/40-class GPU | about 50 to 120 s | about 12 to 30 fps |
| `sycl` | Intel Arc / Iris Xe | about 80 to 180 s | about 8 to 18 fps |
| `hip` | RDNA2 / RDNA3 ROCm host | about 80 to 180 s | about 8 to 18 fps |

The figures are order-of-magnitude only. They depend on the feature extractors
the model enables, on whether `--keep-encodes` is set, and on host I/O
bandwidth.

## Examples

```shell
# Default: auto-pick the fastest backend.
vmaf-tune corpus --source ref.yuv --width 1920 --height 1080 \
    --preset medium --crf 22 --crf 28

# Force CUDA. Fails clearly if nvidia-smi is missing or the vmaf binary
# was built without CUDA support.
vmaf-tune corpus --source ref.yuv --width 1920 --height 1080 \
    --preset medium --crf 22 --score-backend cuda

# Pin CPU for reproducibility against the Netflix golden gate.
vmaf-tune corpus --source ref.yuv --width 1920 --height 1080 \
    --preset medium --crf 22 --score-backend cpu
```

## Cross-backend numeric drift

The CPU is the numerical-correctness floor. GPU backends are held to the
cross-backend parity gate ([ADR-0214](../adr/0214-gpu-parity-ci-gate.md),
`places=4`). Some GPU twins are declared bit-identical to the CPU extractor
(see [exact twins](../development/cross-backend-exact-twins.md)), and the rest
stay inside a measured tolerance. Typical drift is under 1e-3 VMAF points on
1080p / 24 fps content.

For a strict comparison, run the same corpus twice, once with
`--score-backend cpu` and once with `--score-backend <gpu>`, then diff the
`vmaf_score` column of the JSONL output. The per-feature envelopes are in
[`docs/development/cross-backend-gate.md`](../development/cross-backend-gate.md).

## Implementation notes

- The flag is wired in `tools/vmaf-tune/src/vmaftune/cli.py` and resolves to a
  concrete backend through `select_backend()` in
  `tools/vmaf-tune/src/vmaftune/score_backend.py`.
- The resolved backend is passed to the underlying `vmaf` invocation as
  `--backend <name>`. The libvmaf-side selector works per feature, see
  [ADR-0212](../adr/0212-hip-backend-scaffold.md).
- For a one-shot score of a single encode, the same `--backend` flag exists on
  the `vmaf` CLI itself, see [`cli.md`](cli.md).

## History

- [ADR-0299](../adr/0299-vmaf-tune-gpu-score.md): initial CUDA / SYCL / CPU
  shape of the flag.
- [ADR-0314](../adr/0314-vmaf-tune-score-backend-vulkan.md): added Vulkan,
  superseded by ADR-0726.
- [ADR-0667](../adr/0667-vmaf-tune-score-backend-native-priority.md): added
  HIP/ROCm and the native-first `auto` order.
- [ADR-0726](../adr/0726-drop-vulkan-backend.md): removed Vulkan (2026-05-28).

## See also

- [`vmaf-tune.md`](vmaf-tune.md) — the tool overview.
- [`vmaf-tune-codec-adapters.md`](vmaf-tune-codec-adapters.md) — the encoder
  side; `--score-backend` is orthogonal to `--encoder`.
- [`vmaf-tune-fast-path.md`](vmaf-tune-fast-path.md) and
  [`vmaf-tune-compare.md`](vmaf-tune-compare.md) — subcommands that take the
  flag.
- [`docs/backends/cuda/overview.md`](../backends/cuda/overview.md),
  [`sycl/overview.md`](../backends/sycl/overview.md) and
  [`hip/overview.md`](../backends/hip/overview.md) — backend build and runtime
  requirements.
- [Research-0086](../research/0086-usage-doc-coverage-audit-2026-05-08.md) —
  audit that triggered this page.
