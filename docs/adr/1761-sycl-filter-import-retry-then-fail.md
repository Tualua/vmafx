<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1761: The libvmaf_sycl filter retries a failed VA import, then stops naming the frame; each input is imported with its own VA display

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: Lusoris
- **Tags**: sycl, ffmpeg, correctness, fork-local

## Context

When `vmaf_sycl_import_va_surface()` failed, FFmpeg's `libvmaf_sycl` filter
(patch `0005`) logged `VA surface may have been freed by decoder — skipping
frame` and passed the frame on unscored. `frame_cnt` did not advance, so the
pooled score covered fewer frames than were decoded, and nothing in the result
said so. The skip came with the filter itself (#127, ported from a local FFmpeg
checkout). Neither that PR nor a later one names the race it was meant to
survive.

The only recorded failures are in
`docs/research/sycl-zero-copy-nan-evidence/sycl_run.log` (the ADR-1121
investigation): `vaSyncSurface failed: invalid VAContextID`,
`vaExportSurfaceHandle failed: invalid VASurfaceID`, `vaGetImage failed:
invalid VASurfaceID`, on reference imports only, with two hardware devices in
the run. Reading `config_props_sycl()` explains them. The filter takes the VA
display from the main (distorted) input only, and imports the reference
surfaces with it. A reference surface ID either does not exist in that display
(the failure) or names another surface there.

On an Arc A380 with the two decoders on two VA devices, the filter scored 0 of
24 frames equal to the CPU. The pooled score was 54.914218 against 53.419552,
with no error. These failures are not a race, and a retry cannot fix them.

## Decision

Following the maintainer's choice ("Retry, then fail named"):

- The filter reads the VA display of each input's QSV session, and imports
  the reference surfaces with the reference display and the distorted surfaces
  with the distorted display.
- A failed import is tried again, `LIBVMAF_SYCL_IMPORT_TRIES` (3) tries in all,
  with `LIBVMAF_SYCL_IMPORT_PAUSE_US` (1 ms) between them. Each failed try is
  logged with the frame number. The loop is bounded, as HISS-02 asks.
- If the last try fails, the filter stops. The error names the input, the
  surface and the frame (`cannot import the reference VA surface 10 of frame
  2 after 3 tries`). A filter that stopped prints no pooled score and writes
  no log.
- The other `libvmaf_*` filters were checked for the same pattern:
  - the Metal filter fails since ADR-1679. It did print a pooled score over
    the frames before the failure. It now records the stop as the SYCL
    filter does and prints no score after it. Its import failures are a
    surface format or size the import refuses (`-ENOTSUP`, `-EINVAL`), which
    a second try does not change, so it does not retry;
  - the upstream CUDA filter and the generic `libvmaf` filter return errors;
  - the Vulkan filter's pass-through is in code that compiles out since
    ADR-0726 (patch `0006` is kept only for hunk context, ADR-0860), so it is
    left unchanged.
- Found on the way: the software path copied `height / 2` chroma rows, so an
  odd-height frame scored a zero last chroma row. It now copies
  `(height + 1) / 2` rows.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Retry a bounded number of times, then stop with a named error (chosen) | A transient failure costs a few milliseconds, and a persistent one cannot shorten the score silently | A pipeline that used to finish with skipped frames now fails | Maintainer's choice; a score over fewer frames than decoded is a wrong result |
| Stop on the first failure | Simplest | A one-off transient failure ends a long run | Not chosen |
| Keep skipping, and report the number of skipped frames with the score | Pipelines keep running | The pooled score still covers other frames than the input | Not chosen |
| Retry only, without the per-input display | Smallest diff | The recorded failures come from the display mismatch: a retry fails three times, and an ID that exists in the other display still gives a wrong score | The display is the cause; the retry covers what is left |

## Consequences

- **Positive**:
  - a `libvmaf_sycl` or `libvmaf_metal` score covers every decoded frame, or
    there is no score;
  - two VA devices give the one-device scores;
  - odd-height software input gives the CPU's chroma scores.
- **Negative**: a run with a persistent import failure stops. Before, it
  finished with frames missing from the score.
- **Neutral / follow-ups**:
  - `ffmpeg-patches/test/check-sycl-import-retry.sh` injects failures with
    `LD_PRELOAD` and needs an Intel GPU and an FFmpeg with the series;
  - `core/test/test_sycl_filter_import_contract.py` checks the patch without
    a device.

## References

- [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md) (the session
  contract and the evidence run), [ADR-1688](1688-sycl-zero-copy-luma-only-admission.md),
  [ADR-1679](1679-metal-iosurface-biplanar-import.md), [ADR-0183](0183-ffmpeg-libvmaf-sycl-filter.md).
- `T-FFMPEG-SYCL-FILTER-IMPORT-FAILURE-SKIPS-FRAME-2026-10-05`, opened by #2075.
- Source: maintainer popup answer of 2026-10-05, Q "SYCL filter import failure": "Retry, then fail named (Recommended)".
