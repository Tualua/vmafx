- **`vmaf-tune` returns the lowest-bitrate encode that meets a target VMAF in
  every command (breaking).** `recommend` (corpus and live mode, with and
  without `--with-uncertainty`), the ladder's default sampler and the `fast`
  search used to return the smallest CRF that cleared the target, the highest
  bitrate among the passing rows, while `compare` and the bisect already
  returned the cheapest. They now share one rule (ties go to the higher VMAF,
  then the lower CRF) in Python and Go. `fast` also stops returning a CRF that
  misses the target when a cheaper one meets it. A corpus row without
  `bitrate_kbps` is now an error for these picks. Migration: results that read
  the old pick change; rerun `recommend` and the ladder, and pin a CRF
  explicitly where the higher-quality encode is wanted. See ADR-1562.
