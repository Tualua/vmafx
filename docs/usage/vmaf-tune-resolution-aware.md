<!-- markdownlint-disable MD060 -->
# `vmaf-tune` resolution-aware model selection

`vmaf-tune corpus` picks the VMAF model for each source from its encode
height, so a mixed-resolution corpus is scored with the right model. It
needs no flag: the CLI always does it, and every JSONL row records the
model that was used in its `vmaf_model` field.

## Why it matters

VMAF is resolution-aware. The project ships two production pooled-mean
models: `vmaf_v1.0.16_3d0h` (the v1.0.16 standard model for 1080p at 3H
viewing distance) and `vmaf_v1.0.16_1d5h_2160` (its 4K counterpart, 2160p
at 1.5H). Scoring 4K content with the 1080p model under-counts spatial
detail. Scoring 1080p content with the 4K model over-counts coding
artefacts. Either way the bias is several VMAF points, enough to poison a
mixed-resolution ABR-ladder corpus.

## Decision rule

The rule uses the encode height only and mirrors Netflix's published
guidance:

| Encode height | VMAF model |
|---|---|
| `>= 2160` (UHD-1 and up) | `vmaf_v1.0.16_1d5h_2160` |
| `< 2160` (1440p, 1080p, 720p, SD) | `vmaf_v1.0.16_3d0h` |

Width is accepted by the Python API for symmetry but ignored. The project
has no separate anamorphic, 1440p, 720p or SD model, so
`vmaf_v1.0.16_3d0h` is the fallback for everything below 2160p, which
matches Netflix's recommendation.

## Quick start

```shell
vmaf-tune corpus \
    --source ref_4k.yuv --width 3840 --height 2160 --pix-fmt yuv420p \
    --framerate 24 --duration 10 \
    --preset medium --crf 22 \
    --output corpus.jsonl
```

Rows from that job carry:

```json
{"vmaf_model": "vmaf_v1.0.16_1d5h_2160"}
```

## CLI behaviour

- The CLI has no `--resolution-aware` or `--no-resolution-aware` flag.
  `corpus` always selects the model from the height.
- `vmaf_model` is per-row metadata: it records the effective model for
  that row, not a global option. A mixed-ladder corpus legitimately holds
  several distinct values, so group or filter by `vmaf_model` instead of
  assuming one model per file.

!!! warning "`--vmaf-model` and `--neg` do not override the selector"
    In `vmaf-tune corpus` the height rule replaces the model for every
    scored cell, so `--vmaf-model` and `--neg` do not change the
    `vmaf_model` field or the score. To score a corpus with one model
    regardless of height, use the Python API below. To compare models
    from the command line, use a subcommand that honours `--vmaf-model`,
    for example `vmaf-tune compare`.

## Python API

Python callers can switch the selector off per run:

```python
from vmaftune.corpus import CorpusOptions

opts = CorpusOptions(encoder="libx264", resolution_aware=False,
                     vmaf_model="vmaf_v0.6.1")
```

With `resolution_aware=False` the explicit `vmaf_model` is used for every
row, which reproduces a legacy single-model corpus. The decision rule is
also available directly:

```python
from vmaftune.resolution import (
    crf_offset_for_resolution,   # int: -2 / 0 / +2 / +4 by resolution band
    select_vmaf_model,           # Path: in-tree model JSON file
    select_vmaf_model_version,   # str: "vmaf_v1.0.16_3d0h" or "vmaf_v1.0.16_1d5h_2160"
)

assert select_vmaf_model_version(3840, 2160) == "vmaf_v1.0.16_1d5h_2160"
assert select_vmaf_model_version(1920, 1080) == "vmaf_v1.0.16_3d0h"
assert select_vmaf_model(3840, 2160).name == "vmaf_v1.0.16_1d5h_2160.json"
assert crf_offset_for_resolution(1280, 720) == 2
```

A non-positive width or height raises `ValueError`. On the CLI, a missing
`--width` or `--height` is rejected by the required-flag check.

## CRF offset by resolution

`crf_offset_for_resolution(width, height)` is a search-seeding hint for
bisect and ladder code that walks several resolution rungs:

| Encode height | CRF offset |
|---|---|
| `>= 2160` | `-2` |
| `>= 1080` and `< 2160` | `0` |
| `>= 720` and `< 1080` | `+2` |
| `< 720` | `+4` |

The offset is not a quality gate. The values are codec-agnostic and
conservative; later phases are meant to learn per-codec offsets from real
corpora and override them behind the same function signature.

## See also

- [`vmaf-tune.md`](vmaf-tune.md): the base tool.
- [`vmaf-tune-corpus.md`](vmaf-tune-corpus.md): the `corpus` subcommand
  and its row schema.
- [`vmaf-tune-codec-adapters.md`](vmaf-tune-codec-adapters.md): the
  encoders being scored.
- [ADR-0289](../adr/0289-vmaf-tune-resolution-aware.md): the decision.
- [Research-0064](../research/0064-vmaf-tune-resolution-aware.md): model
  selection and CRF-offset rationale.
