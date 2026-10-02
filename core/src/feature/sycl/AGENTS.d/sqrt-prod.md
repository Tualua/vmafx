---
paths:
  - core/src/feature/sycl/sycl_exact_fp.h
  - core/src/feature/sycl/integer_psnr_hvs_sycl.cpp
invariant: sycl_exact_fp.h::sqrt_prod_rn(a, b) = fp32-rounded root of product with bounded error.
---
<!-- markdownlint-disable MD013 MD060 -->
# Fast square root of product

- **`sycl_exact_fp.h::sqrt_prod_rn(a, b)`** (ADR-1401) = fp32-rounded root
  of the exact product, i.e. the host's `(float)sqrt((fp64)a * b)`, with no
  fp64: significands multiplied as `uint64_t` (48 bits), `isqrt_floor50()`
  (25 integer steps), odd floor root rounds up (no tie possible). Exact for
  normal positive operands only; anything else falls to `sqrt_rn(a * b)`.
  Do not replace the integer root by a device `sqrt` of the converted
  product: a third of all operand pairs round differently.
