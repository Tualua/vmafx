<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1423: `adm_hip` takes its weights, shifts and score conclusion from the CPU, folds the denominator once per row and clears its accumulators after the upload

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `hip`, `adm`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

Integer ADM is integer arithmetic up to its last step: the DWT, the CSF, the
contrast masking and the denominator are sums of integers, and only the
conclusion of a scale converts three band accumulators to `float`. A GPU twin
can therefore have the CPU's accumulators exactly, and with the CPU's
conclusion the CPU's scores.

`adm_hip` did not. Measured on a gfx1036 at `--precision max` against the
scalar CPU extractor, on 21 fixture pairs from 18x22 to 3840x2160:

- 19 pairs were identical. A low-detail pair (a gradient against isolated
  impulses, 576x324) differed by 8.1e-8 in `integer_adm_scale2` and 4.0e-7 in
  `integer_adm_scale3`, and BBB 3840x2160 by up to 1.4e-7 in
  `integer_adm_scale0`. With `debug=true` only the denominators differed.
- A 962x13542 frame scored `integer_adm_scale0` 0.860 where the CPU scores
  0.979.
- A context created after another context of the same process returned
  garbage for its first frame (a NaN numerator, and the run failed) when its
  frame was larger than any earlier one's.

Three causes:

1. **The denominator was folded per thread.** The CPU adds the cube terms of
   a row and rounds the row once, `accum += (inner + add_shift_accum) >>
   shift_accum` (`adm_csf_den_fold()`). The kernels of `adm_csf_den.hip`
   rounded each thread's partial sum of a row before adding it. The shift
   discards bits, so the accumulators differ; it shows where the accumulators
   are small (little reference detail) and at scale 0 only once the region
   exceeds 2^20 samples, where the shift is no longer 0.
2. **The kernel derived its shifts on the device.** `ceilf(log2f(area) -
   20.f)` in fp32 is one less than the CPU's `ceil(log2(area) - 20)` for 81
   region areas up to 2^26, each just above a power of two (962x13542 has a
   scale-0 region of 2^21 + 1 samples). The host concluded with the CPU's
   formula, so the denominator was twice too large. The host also kept copies
   of `dwt_quant_step()`, `adm_csf_factors()` and the two `conclude_adm_*()`
   routines, and a `float` border where the CPU computes it in `double`.
3. **The accumulators were cleared ahead of the upload.** Each frame queued
   a `hipMemsetAsync` on the extractor's stream and then uploaded its luma
   planes on the same stream. On the gfx1036 that clear has no effect on the
   frame in the first context of a process that needs larger planes than the
   contexts before it: the kernels add onto what the accumulators held.
   Queued after the upload, or followed by a stream synchronisation, the
   clear works. Fresh device memory is zero, which hid this in a process with
   one context (the `vmaf` tool); a later context gets recycled memory with
   the earlier context's sums in it.

The CUDA twin had the first two in a per-warp form;
[ADR-1416](1416-cuda-adm-cpu-row-rounding.md) fixed it there and opened
`T-HIP-ADM-CSF-DEN-FOLD-PER-THREAD-2026-10-01` for this twin from the source.

## Decision

We will make `adm_hip` return the CPU extractor's values bit for bit.

**The CPU's routines, not copies.** `integer_adm_hip.c` includes
`integer_adm_kernels.h` and defines none of `dwt_quant_step()`,
`adm_csf_factors()`, `conclude_adm_cm()`, `conclude_adm_csf_den()`. The CSF
weights come from `adm_csf_factors()`; the denominator border and every
rounding shift from `adm_csf_den_ctx_init()` / `i4_adm_csf_den_ctx_init()`;
the scores from `adm_cm_result()` / `i4_adm_cm_result()` /
`adm_csf_den_result()` / `i4_adm_csf_den_result()` on the CPU's own contexts.
With `adm_skip_scale0` the numerator is 0 and the denominator the `float`
1e-10 the CPU seeds, and both enter the frame sums.

**One fold per row.** `adm_csf_den.hip` runs one block of 128 threads per row
of the border region and band. Every thread adds the terms of its columns,
the block adds the thread sums in shared memory, and thread 0 folds the row
total once through `adm_csf_den_round_row_total()` (`adm_cm_accumulator.h`,
the function the CPU's `adm_csf_den_fold()` calls since ADR-1416) and adds it
to the band accumulator. The shifts are kernel
arguments; the file evaluates no logarithm.

**The clear follows the upload.** A frame uploads its luma planes
(`adm_hip_stage_luma()`), then queues the clear of the result buffer, then
its kernels. Nothing is cleared at allocation: `hipMemset` on device memory
is itself asynchronous in ROCm 7.2 (hipamd `ihipMemset()` queues it on the
null stream and does not wait), so it orders nothing against the twin's
non-blocking stream.

**Gate.** `EXACT_TWINS` lists `adm`: `hip`
(`scripts/ci/cross_backend_calibration.py`).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| CPU routines on the host, one fold per row on the device, clear after the upload (this ADR) | The CPU's bits on every fixture; no arithmetic of its own left in the host file | The denominator kernels launch one block per row and band instead of one per 1024 columns | Chosen |
| Fold per row, keep the host copies and the device logarithm | Fixes the two measured differences | Leaves the 962x13542 defect and any future drift between the copies and the CPU | The copies are the cause of the largest error |
| A wave or shuffle reduction inside the block | Fewer shared-memory writes | Depends on the wave size (32 or 64 on AMD); the sum is integer, so the order buys nothing | The shared-memory sum is exact and simple |
| Write one total per row and let the host add them | No accumulator to clear, so the memset defect cannot matter | A readback of rows x 3 values per scale, and the contrast-masking kernels would need the same change | Larger change to kernels another lane is editing; queueing the clear after the upload fixes the measured failure |
| Synchronise the stream after every frame's memset | Also cures the first frame | A host wait per frame | The order of two calls costs nothing per frame |
| Clear the accumulators with `hipMemset` when they are allocated | The nine-case test passes with it | Not a wait: on device memory the call is queued on the null stream. The same clear leaves `float_moment_hip`'s first frame wrong | It passed by timing, not by order |
| Keep the tolerance (5e-5) | No code change | Passes a twin whose scale-0 score is 0.12 off on some frame sizes, and whose first frame can be garbage | Not a correctness contract |

## Consequences

- **Positive**: `adm_hip` equals `--backend cpu` at `--precision max` on all
  21 fixture pairs (Netflix 576x324 at 8, 10, 12 and 16 bits and as 4:2:2,
  both 1080p checkerboard pairs, a flat pair, small and odd sizes down to
  18x22, synthetic noise, stripes, impulses and blocks, and BBB 3840x2160
  over 200 frames): 6192 values with `debug=true`, the per-scale numerators
  and denominators included, against the AVX-512 CPU path, and the same
  pairs against the scalar path with BBB over 50 frames. The
  same holds with `adm_csf_mode` 1 to 3, the default model's options,
  `adm_enhn_gain_limit`, `adm_skip_scale0`, another viewing geometry and
  `adm_noise_weight` / `adm_p_norm`. A 962x13542 frame scores the CPU's
  0.979. A later context in a process scores its first frame correctly.
- **Negative**: none measured. `adm_hip` takes 19.15 and 19.11 ms per
  1920x1080 frame and 77.38 and 80.42 ms per 3840x2160 frame before and after
  (medians of seven interleaved runs under other load; the samples overlap).
- **Neutral / follow-ups**: the twin still has no AIM pass
  (`T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05`). Why a
  clear queued ahead of the upload is lost is the runtime's or the driver's
  and not established; `float_moment_hip` and `vif_hip` queue theirs the same
  way and return a wrong first frame in the same situation
  (`T-HIP-FIRST-FRAME-ASYNC-CLEAR-OTHER-TWINS-2026-10-01`). Guards:
  `test_hip_adm_exact` (nine cases, two frames each, `==` on up to 16
  outputs; the low-detail, the shift-boundary and, after smaller contexts,
  the 3840x2160 case fail on the old twin) and
  `test_hip_adm_exact_contract.py` (ten planted regressions, no device).

## References

- `req` (maintainer brief for the HIP lane, 2026-10-01): "adm_hip not identical to scalar CPU at --precision max, independent of adm_enhn_gain_limit (default too) [...] Do: open row, find cause, make adm_hip bit-identical (RC3 = twin exactness, epic #1721), test that fails without fix, add `adm`: hip to EXACT_TWINS when measured identical."
- [ADR-1416](1416-cuda-adm-cpu-row-rounding.md) (the CUDA twin, and the
  row-fold helper), [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the
  exact cell),
  [ADR-1407](1407-hip-strict-fp-every-kernel.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0539](0539-hip-adm-kernels-real.md) (the per-thread
  `atomicAdd` this replaces for the denominator).
- `docs/state.md`: `T-HIP-ADM-NOT-CPU-ARITHMETIC-2026-10-01`,
  `T-HIP-ADM-CSF-DEN-FOLD-PER-THREAD-2026-10-01` and
  `T-HIP-ADM-FIRST-FRAME-STALE-ACCUMULATORS-2026-10-01` (closed by this
  decision).
