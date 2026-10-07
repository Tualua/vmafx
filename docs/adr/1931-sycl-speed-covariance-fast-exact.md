<!-- markdownlint-disable MD013 MD060 -->
# ADR-1931: The SYCL SpEED covariance forms its fp64 terms in parallel and adds them in one sequential chain

- **Status**: Accepted
- **Date**: 2026-10-06 (accepted 2026-10-07)
- **Deciders**: Lusoris Dev (direction chosen by the user, 2026-10-06)
- **Tags**: sycl, speed, bit-exactness, performance, arc-a380, fork-local

## Context

`speed.c::compute_cov_kernel_scalar()` computes each entry of SpEED's 25x25 covariance matrix as one fp64 running sum. It adds `(x - mean_x) * (y - mean_y)` in raster order and rounds every add. `compute_covariance_row()` then stores `(float)(sum / (w * h))`; the contract is in `core/src/feature/speed_cov.h` ([ADR-1459](1459-speed-cov-kernel-exact.md)).

The SYCL twin used to add these terms near-exactly in parallel and round once. On frame 140 of a 3840x1600 10-bit segment this stored the neighbouring fp32 value for one entry, which moved `speed_chroma_u` by one step (`T-SPEED-CHROMA-SYCL-COV-1ULP-2026-10-06`).

Commit `9b1985d6e` fixed it with `covariance_entry()` in `core/src/feature/sycl/sycl_speed_cov_math.h`. That function repeats the reference's operations in its order in soft fp64 (`sycl_soft_signed.h`): two differences, the product and the add. It runs one work-item per (channel, entry), 1300 work-items, each walking 5336 terms. The fix is exact, but it costs about +10 ms per frame on an Arc A380, raising the filter's GPU time from about 20 ms to 30 ms per frame (`T-SYCL-SPEED-COV-EXACT-SEQUENTIAL-COST-2026-10-06`).

The twin is declared exact (`scripts/ci/exact_twins.d/speed_chroma.sycl` and `speed_temporal.sycl`, [ADR-1477](1477-speed-upstream-double-math.md)). Per AGENTS.md section 11, speed lost to exactness is recovered and never traded for a tolerance. The kernels also have to stay:

- free of fp64 ([ADR-0220](0220-sycl-fp64-fallback.md));
- free of scratch memory ([ADR-1395](1395-sycl-kernels-no-scratch.md));
- on sub-group size 16 or 32 ([ADR-1468](1468-sycl-sub-group-sizes-every-aot-target.md));
- device-resident, with no host wait mid-frame and buffers allocated at init ([ADR-1358](1358-sycl-speed-device-resident-linalg.md)).

## Decision

We split `covariance_entry()`'s operations across three launches and keep every operation and its order:

1. **Differences**, parallel over (channel, element, term). `fl64(x - mean)` is formed once for each of the 25 block elements and each pixel of the submatrix. The reference forms the same difference for every entry that uses the element, so 325 entries share 25 x n differences.
2. **Products**, parallel over (channel, term, entry). `fl64(dx * dy)` is formed from the two stored differences and stored as its fp64 bit pattern, term-major.
3. **Add chain**, one work-item per (channel, entry). It runs `signed_add()` over the stored terms in raster order from +0, then does A's quotient and fp32 conversion.

Large geometries run steps 2 and 3 in slices of whole submatrix rows, and the chain's running sum is kept as fp64 bits between slices. The per-term work leaves the 1300 sequential chains, and a chain step drops from 791 to 208 SIMD16 instructions (estimate below).

The exactness argument:

- The operations are `covariance_entry()`'s, called through the same helpers in the same order. Only where each one runs and where its result is stored change.
- A stored value is a normal fp64 value or zero in A's range, so `signed_bits()` and `signed_from_bits()` round-trip it exactly.
- A zero product is +0 in `signed_mul()` and stays +0.

`covariance_entry()` stays in the header. It is the single-function form the device probe test checks, and it calls the same shared helpers.

ISA estimate, AOT for `acm-g11` with the SYCL feature FP line, SIMD16 instructions in the loop body ([evidence](#references)):

| Form | Instructions per dependent step | Chain estimate per 3840x1600 frame |
|---|---|---|
| A: `covariance_entry()`, the whole term per step | 791 | measured +10 ms |
| Split chain (this decision): `signed_add()` on a stored term | 208 (832 per 4 unrolled steps) | about 2.6 ms, plus about 1-1.5 ms of parallel term work and term traffic |
| B1: 128-bit fixed point with RNE to 53 bits | 474 | about 5.9 ms, plus the same term work |

The estimate scales the measured A cost (about 4.7 cycles per instruction: 82 SIMD16 threads cannot hide latency on 128 EUs).

## Measured outcome

Measured on the Arc A380 in plan 13-10 with QSV zero-copy on a 3840x1600 10-bit segment and `vmaf_v1.0.16_3d0h`. Three interleaved, rotated rounds of 600-frame and 20-frame runs, uninstrumented, with one unrecorded warm-up per round. The decision metric is the filter's GPU time per frame.

| Build | GPU ms per frame, median (min-max) | Steady ms per frame, median | Covariance kernels per frame (VTune) |
|---|---|---|---|
| Before the fix (`bce532ce8`, pair sum, not exact) | 18.45 (18.41-18.50) | 20.35 | 0.38 ms |
| A (`9b1985d6e`) | 28.56 (28.53-28.56) | 30.34 | 11.31 ms |
| Split chain (`9f9d0ae64`) | 22.63 (22.58-22.65) | 24.55 | 5.17 ms (differences 0.10, products 0.83, chain 4.24) |

- **Recovery:** the split chain recovers 5.93 of A's 10.11 ms, 58.7 %. That is about 60x the larger min-max range of the two rows (0.07 ms).
- **Remaining cost:** +4.18 ms per frame over the pre-fix kernel. The sequential chain is 4.24 ms of it.
- **Identity:**
  - 200 frames of the segment, zero-copy against FFmpeg `libvmaf` on the CPU: `IDENTICAL frames=200`, for this build and for A.
  - `vmaf --backend cpu` against `--backend sycl` on the dumped 142-frame clip: 0 differences in any feature on any frame. Frame 140 `speed_chroma_u` is 3.644730567932129 on both.
- **Device tests:** `test_sycl_speed_cov_math` (host, device, and the split form on the device), the three SpEED parity tests, `test_sycl_exact_twins` and `test_sycl_kernel_scratch` (139 kernels, none with scratch) pass. So do the zero-copy e2e SpEED cases (`fail=0 nonexact=0`) and `speed_gpu_parity.py` on 576x324.

**Decision:** the split chain is kept. The B2 composition is the follow-up for the remaining 4.2 ms.

Plan 13-07 measured it again on the final head `4ebd77e15`, video only (the harness command plus `-an -sn -dn`), three rounds: GPU time 18.83 ms per frame before the fix (`bce532ce8`), 28.53 ms with A, 23.06 ms with the split chain. A costs +9.70 ms (+51.5 %); the final head costs +4.23 ms (+22.5 %) and recovers 5.47 ms, 56.4 % of A's cost (58.9 % with the harness command, reproducing the 58.7 % above). On the final head `vmaf_v1.0.16_3d0h` scores `IDENTICAL` to CPU libvmaf over 200 frames.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| (a) Keep A only | Exact, already merged, one kernel | +10 ms per frame | Section 11 asks for the recovery |
| (b) B1: terms in parallel, sequential 128-bit fixed-point chain with RNE to 53 significant bits after every add | Exact by a written argument: every running sum is a multiple of the smallest term ulp, and a width bound decides the fast path; A as fallback outside the bound | 474 instructions per step on `acm-g11`, 2.3x the split chain's: the variable 128-bit shifts and the RNE are emulated in 32-bit lanes. Needs a range check, per-entry exponent bounds and a fallback | Strictly more expensive than the chosen design, and its exactness argument has more parts |
| (c) B2: terms in parallel, chunked binade-segmented composition, extending `ordered_sum.h` ([ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md)) to signed terms | Most parallel; could approach +1 ms | `ordered_sum.h` requires non-negative terms. Signed terms let the sum leave and re-enter a binade inside a chunk, so every chunk needs prefix minimum and maximum checks for both parities and a sequential fallback. A cancelling entry (the frame-140 case) crosses binades many times | Follow-up only if the split chain does not recover enough |
| (d) B3: fused, the chain work-item forms each term itself | No term buffer | The product and differences stay inside the 1300 sequential chains: 791 instructions per step, which is A | No gain |
| (e) Covariance on the host | Native fp64 | Breaks the device-resident rule (a mid-frame readback and wait, ADR-1358) | Rejected |
| (f) A tolerance on the entry or the score | None | Moves score bits; forbidden by AGENTS.md section 11 and the exact-twin contract (ADR-1477) | Rejected |
| (g) The chosen split chain | Exact by construction, with no range bound and no fallback; 208 instructions per step | A 55 MB term buffer at 3840x1600 chroma (four channels), written and read once per frame; three launches instead of one | Chosen |

## Consequences

- **Positive**:
  - Measured recovery is 58.7 % of the +10 ms (see Measured outcome).
  - No new arithmetic: the chain is A's.
- **Negative**:
  - Two more device buffers: the differences (25 x n x 8 bytes per channel) and the term slice (325 x slice x 8 bytes per channel).
  - The chain is still sequential: about 5336 dependent adds per entry.
- **Neutral / follow-ups**:
  - The CUDA (`cuda/speed/speed_score.cu`) and HIP (`hip/speed/speed_hip_device.h`) twins keep the old pair-sum design, a latent defect not verified on a device (`T-CUDA-SPEED-COV-PAIR-SUM-SUSPECTED-2026-10-06`, `T-HIP-SPEED-COV-PAIR-SUM-SUSPECTED-2026-10-06`). They are not changed here.
  - B2 is the next step if more recovery is needed.

## References

- Source: `req`, the user's decision on the debug checkpoint, 2026-10-06: «A сейчас, потом B (Recommended)».
- Debug session `T-SPEED-CHROMA-SYCL-COV-1ULP-2026-10-06` (root cause, the fix `9b1985d6e`, the +10 ms measurement); `docs/state.md` rows `T-SPEED-CHROMA-SYCL-COV-1ULP-2026-10-06` and `T-SYCL-SPEED-COV-EXACT-SEQUENTIAL-COST-2026-10-06`.
- ISA estimate: plan 13-10 Task 1, a throwaway translation unit holding the three loops, AOT-compiled for `acm-g11` with IGC shader dumps; the instruction counts are in the table above.
- [ADR-0220](0220-sycl-fp64-fallback.md), [ADR-1395](1395-sycl-kernels-no-scratch.md), [ADR-1468](1468-sycl-sub-group-sizes-every-aot-target.md), [ADR-1358](1358-sycl-speed-device-resident-linalg.md), [ADR-1477](1477-speed-upstream-double-math.md), [ADR-1459](1459-speed-cov-kernel-exact.md), [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md).
