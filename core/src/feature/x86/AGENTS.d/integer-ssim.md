---
paths:
  - core/src/feature/x86/integer_ssim_avx2.c
  - core/src/feature/x86/integer_ssim_avx2.h
  - core/src/feature/integer_ssim.c
invariant: Layout of integer_ssim_moments_t in same order as ssim_moments is a cross-TU invariant.
---
# Integer SSIM Moment Accumulation Invariants

- [ADR-0784](../../../../../docs/adr/0784-integer-ssim-avx2.md) —
  integer SSIM AVX2 horizontal moment accumulation.

## integer_ssim_avx2 — rebase-sensitive invariant (ADR-0784)

`integer_ssim_avx2.c` exports `integer_ssim_accumulate_row_avx2` (8bpc)
and `integer_ssim_accumulate_row_16_avx2` (16bpc), dispatched from
`integer_ssim.c::init()` via function pointers in `IntegerSsimState`.
Layout of `integer_ssim_moments_t` (six consecutive `int64_t` fields
in same order as `ssim_moments`) = cross-TU invariant: changing
field order or inserting padding breaks cast in `calc_ssim()`.
Any upstream change to `ssim_moments` in `integer_ssim.c` must be
mirrored in `integer_ssim_avx2.h`.
