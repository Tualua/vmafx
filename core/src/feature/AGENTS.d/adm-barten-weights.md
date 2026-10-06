---
paths:
  - core/src/feature/adm_csf_fixed_point.h
  - core/src/feature/integer_adm.c
invariant: One power-of-two exponent per scale; weight limits come from adm_csf_fixed_limit(), never from storage.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Integer ADM Barten Weights Exponent Contract

## Integer ADM Barten weights use one exponent per scale (ADR-1325)

`adm_csf_fixed_point.h` is the representation authority for fixed-point ADM
CSF weights on CPU, CUDA, SYCL, HIP, and Metal. Preserve these coupled rules
when rebasing or changing any integer-ADM twin:

- choose one non-negative power-of-two exponent `k` for all three bands of a
  DWT scale; independent band shifts change the metric;
- keep each normalized weight strictly below `adm_csf_fixed_limit(scale, band)`
  (ADR-1472, ADR-1917): 43900 for scale-0 h / v (the int16 1/30 magnitude
  of the CSF stage binds before the cube's 46603.4), 2^16 for scale-0 d, 279958309,
  539893111, 546406567 at scales 1..3. Limit = contrast-masking excess budget
  (`(v * v + round) >> 29|30` must fit int32: 2^30 - 1 / 1518500249) divided
  by largest wavelet coefficient of the scale (`ADM_DWT_BAND_MAX_SCALE0..3`,
  half the absolute sum of the composite filter). Old limit 2^30 wrapped the
  square in Barten mode: NaN numerator (10 px checkerboard), `integer_adm2`
  0.587 for 0.784 without a message (1 px). Never raise a limit, never widen
  or saturate the square in one implementation only;
- changed DWT taps or shifts, `shift_sq`, `i4_shift_dst` -> band bounds
  change: update the constants and `BAND_FORMAT` in
  `core/test/test_integer_adm_cm_budget.c` together (the test derives the
  bounds from the taps and fails until they agree);
- restore `3k` in the host contrast-masking finalizer because the accumulated
  signal is cubed, while the denominator continues to use the original float
  CSF factors;
- keep the `k=0` fixed-point values and AVX2/AVX-512 dispatch unchanged;
  configurations needing normalization use scalar weighted-CSF/CM stages but
  may retain SIMD DWT, decoupling, and denominator stages;
- reject negative or non-finite table output, including the blend tables'
  negative sentinel, instead of converting it to unsigned.
- reject every viewing geometry where
  `adm_norm_view_dist * adm_ref_display_height < 3240` through the shared
  `adm_viewing_geometry_check(extractor, nvd, rdh)` helper; this floor is independent of CSF mode.
  When rejected, it logs a named refusal naming the extractor, option values, product,
  the 3240 floor (`1080p at 3H`), and `float_adm` as the accepting alternative.
  The CPU reference checks in `extract()`; GPU twins check during `init()`.

`test_integer_adm_cm_budget` holds the limits against the taps and scores
adversarial frames against `float_adm`.
`test_adm_csf_representable` pins finite, non-degenerate CPU mode-1 output;
the CUDA, SYCL, HIP, and Metal parity fixtures pin their supported scores at
places=4. Metal integer ADM implements modes 0..3 and therefore must not regain
`VMAF_OPT_FLAG_DEFAULT_ONLY` on `adm_csf_mode`.
