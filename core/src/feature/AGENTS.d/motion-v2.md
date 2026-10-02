---
paths:
  - core/src/feature/integer_motion_v2.c
  - core/src/feature/motion_tools.h
invariant: Motion v2 option-surface parity, five-frame-window rejection, and NEON shift semantics.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Motion v2 Option Surface and NEON Shift Semantics

- **`motion_v2` public option-surface duplicates motion v1**
  (fork-local, ADR-0337):
  [`integer_motion_v2.c`](../integer_motion_v2.c) registers its own
  `VmafOption[]` table for seven motion knobs
  (`motion_force_zero`, `motion_blend_factor`, `motion_blend_offset`,
  `motion_fps_weight`, `motion_max_val`, `motion_five_frame_window`,
  `motion_moving_average`) — duplicating motion v1's
  [`integer_motion.c`](../integer_motion.c) surface byte-for-byte
  against upstream Netflix `4e469601`. duplication is
  deliberate; v1 and v2 are independent extractors with independent
  output namespaces (`VMAF_integer_feature_motion*_score` vs
  `…_v2_score`). On rebase: when touching one extractor's option
  help string, touch other; when upstream touches option
  table, port change to **both** extractors. ADR-0141 catches
  drift on next edit.
- **`motion_v2` rejects `motion_five_frame_window=true`**
  (fork-local, ADR-0337): `init()` returns `-ENOTSUP` and logs
  pointer at ADR. 5-frame mode requires `prev_prev_ref`
  field on `VmafFeatureExtractor` plus `n_threads * 2 + 2`
  picture-pool sizing in `vmaf_read_pictures` (upstream `a2b59b77`)
  that conflicts with fork's [ADR-0152](../../../../docs/adr/0152-vmaf-read-pictures-monotonic-index.md)
  `read_pictures*` decomposition. picture-pool refactor is
  deferred to its own PR. Mirrors [ADR-0219](../../../../docs/adr/0219-motion3-gpu-coverage.md)
  §Decision's GPU motion3 `-ENOTSUP` precedent. On rebase: when
  picture-pool refactor PR lands, flip `-ENOTSUP` guard to
  `prev_prev_ref` lookup and reinstate `min_idx = 5? 2 : 1`
  branching in `flush()` (currently collapsed to `min_idx = 1`
  per ADR-0337's deferral). See
  [rebase-notes ADR-0337](../../../../docs/rebase-notes.md) for
  deferred-hunks ledger.
- **`motion_v2` NEON shift semantics** (fork-local, ADR-0145):
  [`arm64/motion_v2_neon.c`](../arm64/motion_v2_neon.c) uses
  **arithmetic** right-shift throughout (`vshrq_n_s64(v, 16)` for
  Phase-2 known shift, `vshlq_s64(v, -(int64_t)bpc)` for
  Phase-1 runtime shift). fork's AVX2 variant
  [`x86/motion_v2_avx2.c`](../x86/motion_v2_avx2.c) uses
  `_mm256_srlv_epi64` (*logical*) which can diverge from scalar on
  negative-diff pixels. NEON matches scalar, AVX2 does not — this
  is intentional until AVX2 audit lands. On rebase: keep
  arithmetic-shift form in NEON; do NOT port AVX2's logical pattern
  even if it looks simpler. 4-lane stride + scalar tails on both
  sides of row are load-bearing for x_conv edge-mirror
  contract. See
  [ADR-0145](../../../../docs/adr/0145-motion-v2-neon-bitexact.md)
  and [rebase-notes 0038](../../../../docs/rebase-notes.md).
