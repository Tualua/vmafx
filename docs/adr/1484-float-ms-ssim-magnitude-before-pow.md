<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1484: `float_ms_ssim` takes the magnitude of a scale's terms before `pow()`; upstream raises a negative structure term to a fractional power and returns NaN on anti-correlated frames

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `ms-ssim`, `correctness`, `numerics`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR that names it and gives its size (see
References). [ADR-1033](1033-cpu-scoring-nan-ub-guards.md) (item 2) made this
change as one of eleven guards and says neither what upstream does nor how
far the fork is from it. ADR-1033 is accepted and is not edited; this ADR adds
the comparison.

Upstream, `libvmaf/src/feature/ms_ssim.c:294` at Netflix `9e48141b`:

```c
msssim *= pow(l, alphas[idx]) * pow(c, betas[idx]) * pow(s, gammas[idx]);
```

`s`, the structure term of a scale, is a correlation and is negative where
the reference and the distorted picture are anti-correlated. The exponents
are fractional, so `pow()` returns NaN and the frame's `float_ms_ssim` is
NaN. The Rouse path of the same file already takes the magnitude of the same
term (`ms_ssim.c:86`).

The fork, `core/src/feature/ms_ssim.c`, since PR #641 (`a295f4a70`,
2026-06-04):

```c
msssim *= pow(fabs((double)l), (double)alphas[idx]) *
          pow(fabs((double)c), (double)betas[idx]) *
          pow(fabs((double)s), (double)gammas[idx]);
```

## Decision

The fork keeps the magnitude before `pow()`. Where a scale's structure term
is negative the fork returns a finite `float_ms_ssim` and upstream NaN; where
all three terms are non-negative the expression is upstream's value.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's expression | Same code as upstream | The score is NaN on such frames; in the fork a non-finite score fails the frame ([ADR-1302](1302-nonfinite-scores-fail-the-frame.md)), so the run would end with an error where it now returns a value | The magnitude is what the file's own Rouse path does |
| Magnitude of `s` only, as the fork's upstream pull request does | The smallest change | None in value: `l` and `c` are built from non-negative means and standard deviations | Equal in value; the fork's line predates the pull request. At the sync that brings upstream's fix the fork takes upstream's line |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose
  `ms_ssim.c` is `9e48141b`'s, against the fork with its unintended
  differences reverted; GCC 16.2.1, x86-64, C API at `%.17g`, scalar and
  default dispatch): on the 1920x1080 checkerboard pair with 10 pixel squares
  upstream returns NaN on 3 of 3 frames and the fork 0.9895 (frame 2:
  0.98950781588819681). No other fixture of the audit has a negative term:
  with this change and the separable decimation
  ([ADR-0125](0125-ms-ssim-decimate-simd.md)) undone in a scratch build,
  `float_ms_ssim` was identical to upstream on 18 fixtures and 4 option
  variants.
- **Upstream status**: Netflix/vmaf#1665 (open, sent by the fork) takes the
  magnitude of `s` only.
- **Ends when** upstream merges #1665. The allowlist entry of the upstream
  parity guard then goes stale and is removed.

## References

- Fork PR #641 (`a295f4a70`); [ADR-1033](1033-cpu-scoring-nan-ub-guards.md)
  item 2.
- Upstream: `libvmaf/src/feature/ms_ssim.c:86`, `:294` at Netflix
  `9e48141b`; Netflix/vmaf#1665.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
