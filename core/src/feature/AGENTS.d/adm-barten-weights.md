---
paths:
  - core/src/feature/adm_csf_fixed_point.h
  - core/src/feature/integer_adm.c
invariant: Integer ADM Barten weights use one power-of-two exponent per scale.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Integer ADM Barten Weights Exponent Contract

## Integer ADM Barten weights use one exponent per scale (ADR-1325)

`adm_csf_fixed_point.h` is the representation authority for fixed-point ADM
CSF weights on CPU, CUDA, SYCL, HIP, and Metal. Preserve these coupled rules
when rebasing or changing any integer-ADM twin:

- choose one non-negative power-of-two exponent `k` for all three bands of a
  DWT scale; independent band shifts change the metric;
- keep normalized scale-0 weights strictly below 2^16 and scale-1..3 weights
  strictly below 2^30, retaining two headroom bits for signed CSF/CM and cube
  arithmetic;
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
  `adm_viewing_geometry_check()` helper; this floor is independent of CSF mode.
  The CPU reference checks before computation; GPU twins check before
  normalization or device work.

`test_adm_csf_representable` pins finite, non-degenerate CPU mode-1 output;
the CUDA, SYCL, HIP, and Metal parity fixtures pin their supported scores at
places=4. Metal integer ADM implements modes 0..3 and therefore must not regain
`VMAF_OPT_FLAG_DEFAULT_ONLY` on `adm_csf_mode`.
