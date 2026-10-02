---
paths:
  - scripts/ci/check-win64-stack-alignment.py
  - core/src/feature/x86/ssim_avx512.c
invariant: A kernel here must not hold enough vector values live to make compiler spill one to stack.
---
# Wide Vector Register Pressure and Stack Alignment

## Wide vector register pressure is a Windows correctness constraint (ADR-1254)

A kernel here must not hold enough `__m512` / `__m256` values live to make
compiler spill one to stack. gcc's MinGW target allocates 32/64-byte-aligned
spill slots addressed off `%rsp`. MS x64 ABI guarantees only 16-byte
alignment; its unwind contract prevents frame realignment gcc performs on
SysV. Spilled `ymm` / `zmm` = general-protection fault on most call
paths, surfacing as access violation on `0xFFFFFFFFFFFFFFFF`.

Concretely: `ssim_accumulate_avx512` rebuilds broadcast constants per block
instead of hoisting them across loop. Keep it that way; same applies to
any kernel gaining vector-valued loop invariants.

Review cannot see this — it is register-allocator decision, and CI's Windows
runners have no AVX-512, so test leg never executes these kernels.
`scripts/ci/check-win64-stack-alignment.py` reads emitted code on
`Windows MinGW64` lane instead. When it fires, reduce pressure in named
function rather than suppressing it. See
[Research-2061](../../../../../docs/research/2061-win64-cannot-realign-the-stack.md).
