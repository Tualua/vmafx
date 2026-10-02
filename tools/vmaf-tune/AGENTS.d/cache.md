---
paths:
  - tools/vmaf-tune/src/vmaftune/cache.py
  - tools/vmaf-tune/tests/test_cache.py
invariant: Cache key fields are load-bearing; cache content stays opaque and parsed through domain model.
---
<!-- markdownlint-disable MD024 -->
# Execution caching

- **Cache key fields are load-bearing
  ([ADR-0298](../../../docs/adr/0298-vmaf-tune-cache.md)).**
  `cache_key()` function in `cache.py` digests six fields:
  `src_sha256`, `encoder`, `preset`, `crf`, `adapter_version`,
  `ffmpeg_version`. Dropping any one of them is silent correctness
  bug — stale entries shadow real results when adapter or ffmpeg
  is upgraded. Contract is asserted by
  `test_cache_key_diffs_on_each_field`. When adding new codec
  adapter, set `adapter_version: str` on dataclass; registry
  `Protocol` already requires it. Bump string when adapter's argv
  shape, preset list, or quality range changes.
- **Cache content stays opaque.** Cache value is parsed
  `(bitrate, vmaf, encode_time, score_time)` tuple plus opaque
  `<key>.bin` blob. Do not bake cache contents into JSONL row —
  row is canonical record, cache is sidecar. Cache hit must
  produce row that is bit-identical to cache miss (modulo
  `encode_path`, which stays empty unless `--keep-encodes`).
