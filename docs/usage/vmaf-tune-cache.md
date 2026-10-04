<!-- markdownlint-disable MD060 -->
# `vmaf-tune` content-addressed cache

The `vmaf-tune` cache stores the encode and score result of every `corpus` cell
under a content hash, so a repeated `(source, encoder, preset, crf)` cell is
restored from disk instead of being encoded and scored again. It is a Python
API feature: there is no command-line flag, and it is **off by default**. The
design is in [ADR-0298](../adr/0298-vmaf-tune-cache.md); the tool overview is
in [`vmaf-tune.md`](vmaf-tune.md).

## Turn it on

The corpus runner enables the cache when both `cache_enabled` and `cache_dir`
are set on `CorpusOptions`:

```python
from pathlib import Path

from vmaftune.corpus import CorpusJob, CorpusOptions, iter_rows

opts = CorpusOptions(
    encoder="libx264",
    output=Path("corpus.jsonl"),
    cache_enabled=True,
    cache_dir=Path.home() / ".cache" / "vmaf-tune",
)
rows = list(iter_rows(job, opts))  # job: a CorpusJob for one source
```

| Option | Default | Meaning |
|---|---|---|
| `cache_enabled` | `False` | Master switch for the cache. |
| `cache_dir` | `None` | Cache root. The cache stays off while this is `None`, even with `cache_enabled=True`. |

`vmaf-tune corpus` has no `--cache-dir`, `--no-cache` or size flag. The only
`--cache-dir` on the command line belongs to the `sidecar` subcommand and is a
different cache.

`default_cache_dir()` in `tools/vmaf-tune/src/vmaftune/cache.py` returns
`$XDG_CACHE_HOME/vmaf-tune` (or `~/.cache/vmaf-tune` when the variable is
unset). The corpus runner does not call it, so pass that path yourself as
`cache_dir` if you want it.

## Cache key

A cache entry is keyed on the SHA-256 of the canonical-JSON encoding of six
fields:

| Field | Source |
|---|---|
| `src_sha256` | Content hash of the reference file. |
| `encoder` | Adapter slug (`libx264`, `hevc_nvenc`, ...). |
| `preset` | Encoder preset string fed to the adapter. |
| `crf` | Quality-knob value (int). |
| `adapter_version` | Meant to change when the adapter's argv shape changes. |
| `ffmpeg_version` | Meant to track the host ffmpeg version string. |

`cache_key()` requires all six, and `tools/vmaf-tune/tests/test_cache.py`
checks that changing any one of them produces a new key.

!!! warning "What the corpus runner actually keys on"
    The corpus runner passes an empty string for both `adapter_version` and
    `ffmpeg_version` (see `iter_rows` in
    `tools/vmaf-tune/src/vmaftune/corpus.py`),
    so today a hit depends on `src_sha256`, `encoder`, `preset` and `crf`
    only. An adapter or ffmpeg upgrade does not invalidate entries; clear the
    cache directory after one. Options that are not part of the key, such as
    the VMAF model, the score backend or the sample-clip length, do not
    invalidate entries either.

## Cache layout on disk

```text
<cache_dir>/
  meta/<key>.json     parsed result tuple (size, times, vmaf score, versions)
  blobs/<key>.bin     opaque encoded artifact (atomic put: tmp + rename)
  __index__.json      last-access timestamps for LRU eviction
```

## Cache lifecycle

- **Hit:** the encode and score subprocesses are skipped and the cached tuple
  becomes the JSONL row. The row gets a fresh `run_id` and timestamp.
- **Miss:** encode and score run as normal. A successful result (encoder exit
  status 0) is inserted with a fresh `last_access` timestamp.
- **Source hash off:** a run with `--no-source-hash` has no `src_sha256`, so
  the cache is skipped for every cell of that run.
- **LRU eviction:** each insert evicts least-recently-used entries until the
  total size of `meta/` plus `blobs/` is at or below the ceiling. The default
  ceiling is 10 GiB (`DEFAULT_SIZE_BYTES`). The corpus runner does not expose
  a size option; use `TuneCache(path, size_bytes=...)` directly to change it.
- **Index flush:** the runner writes `__index__.json` once, after the sweep.

## Caveats

- The cache is not baked into the JSONL row. The row stays the canonical
  record and the cache is an opaque result store, so rows look identical
  whether a cell hit or missed ([ADR-0298](../adr/0298-vmaf-tune-cache.md)).
- A hit does not write a synthetic `encode_path`; that field stays empty unless
  `--keep-encodes` is set.
- `TuneCache` is not thread-safe. Concurrent processes (for example over NFS)
  can share a directory: reads work, and writes are last-writer-wins with both
  writers' bytes valid by content addressing.

## Manual eviction

There is no cache prune subcommand. To start over, for example when the corpus
methodology changes incompatibly, delete the directory:

```shell
rm -rf ~/.cache/vmaf-tune
```

## See also

- [`vmaf-tune.md`](vmaf-tune.md) — the tool overview.
- [`vmaf-tune-corpus.md`](vmaf-tune-corpus.md) — the `corpus` subcommand whose
  cells the cache stores.
- [`vmaf-tune-codec-adapters.md`](vmaf-tune-codec-adapters.md) — the adapters
  that `adapter_version` refers to.
- [ADR-0298](../adr/0298-vmaf-tune-cache.md) — design decision.
- [Research-0086](../research/0086-usage-doc-coverage-audit-2026-05-08.md) —
  audit that triggered this page.
