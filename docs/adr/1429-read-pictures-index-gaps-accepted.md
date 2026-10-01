<!-- markdownlint-disable MD013 MD060 -->
# ADR-1429: `vmaf_read_pictures()` accepts an index that skips values and documents what the motion scores do

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: api, docs, motion, netflix-compat, fork-local

## Context

Netflix/vmaf#910 reported that `vmaf_read_pictures()` accepts an index that does not follow the previous one and then produces wrong `motion2` scores without any message. The fork rejects a repeated or earlier index since [ADR-0152](0152-vmaf-read-pictures-monotonic-index.md) and returns `-EINVAL`; the Netflix 576x324 pair submitted as 0, 1, 2, 3, 4, 5 and as 0, 1, 2, 4, 3, 5 shows it (the `3` after the `4` returns `-EINVAL`). An index that jumps forward (0, 1, 2, 4, 5) is accepted. Measured on 2026-10-01 against master `ae5e22d09`: indices 0 to 2 score, and after the flush `motion2` of indices 3 to 5 returns `-EAGAIN` (index 3 was never submitted; 4 and 5 have no predecessor in the motion extractor's window). Index 2 carries the `motion2` the last picture of a stream gets, 4.214327, instead of the 4.071596 it has with index 3 present. No call returns a wrong number silently: the later pictures fail loudly, and the picture before the gap is the stream's last picture as far as the extractor can tell.

Whether to reject a gap was open: features that look at one picture (`psnr`, `ssim`, `vif`, `adm`, `cambi`) are independent of it, and a caller may have scored only the pictures it needs.

## Decision

We will keep accepting an index that skips values and state the consequence in `libvmaf.h` and `docs/api/index.md`: submit indices 0, 1, 2, ... without gaps whenever a model uses `motion2` or `motion3`; after a gap those two scores are not written for the pictures that follow (queries return `-EAGAIN`, also after the flush) and the picture before the gap gets the last picture's `motion2`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Reject any index other than previous + 1 with `-EINVAL` | A gap can never reach a motion extractor | Breaks callers that score a sparse set of pictures with single-picture features (a stream that starts above 0 already fails at its first call with the `vmaf_v0.6.1` features: `-EINVAL` for indices 1, 2, 3 on 2026-10-01) | A behaviour change for correct programs to protect an incorrect one; the motion consequence is an error, not a wrong value |
| Reject a gap only when a motion extractor is registered | Protects exactly the affected case | Registration order is free (`vmaf_use_feature()` after the first picture is allowed), so the check would depend on call history; a model that gains `motion2` later changes the contract | Stateful and surprising |
| Fill the gap by repeating the previous picture | Scores for every index | Invents pictures; the scores would belong to frames the caller never submitted | Wrong values presented as measurements |

## Consequences

- **Positive**: the contract a caller has to follow is written where they read it, and the observed outcome of breaking it is on record.
- **Negative**: a gap is still accepted, so a caller that ignores the docs gets `-EAGAIN` after the flush rather than a failure at the call that skipped.
- **Neutral / follow-ups**: none; no behaviour changes.

## References

- Netflix/vmaf#910; the fork's reproducer is `repro910` (indices 0,1,2,4,5 above).
- Per user direction (2026-10-01): check on the fork whether the upstream defects found on `6ec23e8f2` reproduce, and fix or document each (RC3, [ADR-1421](1421-rc3-rc8-candidate-map.md)).
