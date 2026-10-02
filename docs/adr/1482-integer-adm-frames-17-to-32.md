<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1482: integer `adm` on frames of 17 to 32 pixels reads inside the frame and rounds a zero shift with 0; upstream reads index -1 there, and scale 3 differs by up to 0.23

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `adm`, `memory-safety`, `correctness`, `simd`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). The upstream parity audit
of 2026-10-02 found this difference recorded in `docs/state.md` and in
Research-2063, but in no ADR.

Two defects of upstream's integer ADM show only on frames with a dimension of
17 to 32 pixels, at Netflix `9e48141b`:

1. `dwt2_src_indices_filt()` (`libvmaf/src/feature/integer_adm.c:708` to
   `:753`). At the fourth wavelet level such a dimension has a subsampled
   extent of 2. The mirrored tail loop then starts at `h_half - 2 = 0` and
   overwrites the entry for index 0, `{1, 0, 1, 2}`, with `{-1, 0, 1, 2}`:
   scale 3 reads row -1 and column -1. The first loop's bound
   `h_half - 2` is unsigned and wraps for an extent below 2.
2. The rounding constant of the contrast-masking cube,
   `(uint32_t)pow(2, shift - 1)` (`integer_adm.c:1564` to `:1573`, and the
   same expression in `x86/adm_avx2.c` and `x86/adm_avx512.c`). For a frame
   17 to 32 pixels wide the scale-0 shift is 0, `shift - 1` wraps, and the
   conversion of the infinite power to `uint32_t` is undefined. In the
   builds the fork measured (`docs/state.md`, second row under References)
   the scalar and AVX2 code got 0 and the AVX-512 code `0xFFFFFFFF`.

The fork fixed both on `port/upstream-2026-09` (PR #1473, with the GPU twins
following in PR #1507, `7e20ab78d`): `dwt2_src_indices_1d()` in
`core/src/feature/integer_adm.c` starts the mirrored tail at
`max(1, n_half - 2)` and its first loop no longer wraps, and every rounding
constant calls `adm_half_shift()` (`core/src/feature/adm_csf_fixed_point.h`),
which returns 0 for a shift of 0. Frames of 16 pixels or less are refused by
`adm_frame_size_check()`; that refusal is part of
[ADR-1481](1481-extractor-failure-fails-the-run.md)'s list.

## Decision

The fork keeps both fixes. Integer `adm` on frames with a dimension of 17 to
32 pixels differs from upstream by design; frames with both dimensions of 33
or more are not affected.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's index table and rounding constant | Same code as upstream | Scale 3 reads before the buffer (a heap-buffer-overflow under ASan in `adm_dwt2_s123_combined_*`), and its value changed from run to run on identical input before upstream zeroed the buffer; the AVX-512 path differs from the scalar path at scale 0 | An out-of-bounds read and an undefined conversion are not a reference |
| Refuse frames below 33 pixels | No value that differs from upstream | Refuses 17 to 32, which upstream accepts and which a correct index table scores | The fix is local and tested |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose
  `integer_adm.c` is `9e48141b`'s on x86, against the fork with its unintended
  differences reverted; GCC 16.2.1, x86-64, C API at `%.17g`; the 18x22,
  17x17, 19x19 and 24x24 pairs, 9 frames): scalar dispatch,
  `integer_adm_scale3` differs on 9 of 9 frames by up to 0.23 (18x22: upstream
  0.91209581535961382, fork 1.1421883345409307), `adm2` by up to 0.104,
  `adm3` by up to 0.052, `aim` by up to 0.021. Default dispatch adds
  `integer_adm_scale0` (up to 3.5e-6) and `aim` (up to 0.0014), where
  upstream's AVX-512 path differs from its own scalar path.
- **Upstream status**: Netflix/vmaf#1599 (the index table) and #1600 (the
  rounding constant), both open, sent by the fork.
- **Ends when** upstream merges #1599 and #1600. With upstream's pull request
  applied the fork's values are upstream's (24x24: `integer_adm_scale3`
  0.986957 / 0.973522 / 0.945018 in both, `docs/state.md` row
  `Netflix/vmaf#1599`); the allowlist entry of the upstream parity guard then
  goes stale and is removed.
- **Neutral**: `test_integer_adm_tiny_frames` runs nine tiny geometries twice
  with the heap scribbled between the runs, and compares the default dispatch
  with the scalar path across widths 17 to 32.

## References

- Fork PR #1473 and PR #1507 (`7e20ab78d`); `docs/state.md` rows
  `T-ADM-SCALE3-TINY-FRAME-OOB-READ-2026-09-18` and
  `T-ADM-AVX512-SMALL-WIDTH-SCALE0-2026-09-18`;
  [Research-2063](../research/2063-upstream-sync-2026-09-adm-vif-simd.md).
- Upstream: `libvmaf/src/feature/integer_adm.c:708` to `:753`, `:1564` to
  `:1573` at Netflix `9e48141b`; Netflix/vmaf#1599, Netflix/vmaf#1600.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
