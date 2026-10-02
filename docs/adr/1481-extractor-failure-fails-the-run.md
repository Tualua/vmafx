<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1481: an extractor that fails on a worker thread fails the run; upstream drops the worker's error and returns success without the metric

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `core`, `error-handling`, `threading`, `correctness`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). The upstream parity audit
of 2026-10-02 found this difference without one.

Upstream, at Netflix `9e48141b`: with a thread pool (`n_threads` of 1 or
more, `libvmaf/src/libvmaf.c:139`), pictures are scored by
`threaded_extract_batch_func()` (`libvmaf.c:492` to `:566`). The function
returns `void`. When an extractor's `init` or `extract` fails it stores the
error in `f->err`, a field of the job's private copy of the request, and
returns. The pool calls `job->func(job->data, &worker->data)` and has nothing
to keep (`libvmaf/src/thread_pool.c:94`), and `vmaf_thread_pool_wait()`
returns 0 (`thread_pool.c:191` to `:200`). `vmaf_read_pictures()` and the
flush therefore report success, the failed extractor has written no value, and
the caller learns of it only when a score it asks for is missing.

The fork changed this in PR #871 (`32ec0aa64`, 2026-06-12), as one item of a
bundle of sanitizer findings with no ADR: the job function returns `int`, the
pool accumulates a non-zero return in `last_error`
(`core/src/thread_pool.c`), `vmaf_thread_pool_wait()` returns and clears it,
and `threaded_extract_batch_func()` returns `f->err`
(`core/src/libvmaf.c`). A failed extractor now fails the flush (or the next
`vmaf_read_pictures()`), and the run ends with the extractor's error code.

## Decision

The fork keeps the propagation: an extractor failure on a worker thread is an
error of the run. Where upstream returns success without the metric, or
crashes, the fork returns the error; this is a difference in status, not in
any value both trees compute.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Drop the worker's error as upstream does | Same exit status as upstream | A run that computed nothing for a requested feature reports success; a model whose feature is missing fails later with an unrelated message; with several features the output silently lacks one | An error that is not reported is the defect PR #871 fixed |
| Log the failure and still return success | Visible in the log | Callers of the C API do not read logs; the Python harness and FFmpeg would keep consuming incomplete output | Same defect for every API user |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose
  `libvmaf.c` and `thread_pool.c` are `9e48141b`'s, against the fork with its
  unintended differences reverted; C API with one worker thread, scalar and
  default dispatch; runs counted per fixture, dispatch and option set):
  - Upstream exits 0 with no value for the feature, the fork returns
    `-EINVAL`: `cambi` on frames below its minimum (58 runs: 160x90 and
    smaller), integer `adm` with `adm_norm_view_dist=1.5` (10), `psnr_hvs` at
    16 bits (6), `ciede` and `speed_chroma` on 4:0:0 (2 each).
  - Upstream ends in a segmentation fault, the fork returns `-EINVAL`:
    `speed_chroma` on frames too small for one block (56 runs),
    `speed_temporal` on such frames (20), integer `adm` at 16 pixels or below
    (12).
  - Both fail: `float_ms_ssim` below 176x176 (28 runs; upstream prints
    `error: scale below 1x1!`, the fork refuses at init,
    [ADR-0153](0153-float-ms-ssim-min-dim-netflix-1414.md)).
  No value that both trees emit is changed by this deviation.
- **Upstream status**: the fork has sent pull requests that make single
  extractors refuse what they cannot compute: Netflix/vmaf#1620 (SpEED),
  #1637 (`float_ms_ssim`), #1642 (integer `adm`, with the report
  Netflix/vmaf#1607), #1629 (`cambi`). All are open. None of them changes the
  thread pool: with all four merged, upstream would still return success when
  such an `init` fails on a worker.
- **Ends when** upstream propagates a worker's error to the caller. No
  upstream pull request does that today. Until then the allowlist of the
  upstream parity guard lists the affected extractor and frame-size
  combinations as differences of kind `error`.
- **Neutral**: which frames an extractor refuses is each extractor's own
  decision (ADR-0153 for `float_ms_ssim`,
  [ADR-1302](1302-nonfinite-scores-fail-the-frame.md) for non-finite scores,
  the `docs/state.md` rows of the single fixes). This ADR records only that a
  refusal reaches the caller.
- **Neutral**: `core/test/test_thread_pool.c` and `core/test/test_framesync.c`
  hold the job signature and the returned error.

## References

- Fork PR #871 (`32ec0aa64`), item "thread-pool error propagation".
- Upstream: `libvmaf/src/libvmaf.c:139`, `:492` to `:566`;
  `libvmaf/src/thread_pool.c:94`, `:191` to `:200` at Netflix `9e48141b`;
  Netflix/vmaf#1620, #1637, #1642, #1629, #1607.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
