<!-- markdownlint-disable MD013 MD029 -->
# Performance claims aggregator (May 2026, historical)

This page is a frozen record of 11 performance claims copied from the
descriptions of PRs shipped in May 2026. It is not current guidance.

!!! warning "Historical, unverified"
    The numbers below are claims made in PR descriptions. No benchmark
    artifact, hardware, or baseline file is cited for any of them, and the
    page is not refreshed. Entries 1 to 5 describe the Vulkan backend, which
    was removed ([ADR-0726](../adr/0726-drop-vulkan-backend.md)); the code
    they describe no longer exists. For current measurement workflow read
    [perf.md](perf.md).

## Summary

| Item | Count |
| --- | --- |
| Total PRs | 11 |
| Measured claims (as stated in the PR) | 8 |
| Infrastructure, no direct claim | 3 |

## Vulkan submit-pool migration (backend removed)

1. **PR #561**: Vulkan submit-pool infrastructure. *Claim*: "Foundation for
   pooled command buffer reuse; no direct user-visible perf delta
   (infrastructure)." Status: Infrastructure; measurement pending on real
   workloads.

2. **PR #562**: Vulkan submit-pool refactor. *Claim*: "Consolidates queue
   submission; latency reduction via batch submission (measured: ~5–8% on
   feature extraction)." Status: Measured.

3. **PR #563**: Vulkan submit-pool optimization. *Claim*: "Further batch
   reduction; memory traffic optimization (measured: ~2–3% on some kernels)."
   Status: Measured.

4. **PR #564**: Vulkan buffer pool lifecycle. *Claim*: "Reduces per-frame
   allocation churn; no direct perf claim (memory hygiene)." Status:
   Infrastructure; measurement pending.

5. **PR #565**: Vulkan submit-pool finalization. *Claim*: "Consolidates pooling
   across all features; cumulative effect expected (measured: validated against
   baseline suite)." Status: Measured.

## CUDA improvements

6. **PR #569**: CUDA kernel optimization. *Claim*: "Improved occupancy +
   register pressure; perf gain varies by feature (measured: 3–12% on
   motion/cambi kernels)." Status: Measured.

7. **PR #571**: CUDA memory layout. *Claim*: "Coalescing improvements; latency
   reduction (measured: ~6% on bandwidth-bound features)." Status: Measured.

## Motion v2 AVX2 fix

8. **PR #587**: Motion v2 AVX2 correctness fix. *Claim*: "No perf claim (bug fix
   to bit-exactness vs. scalar)." Status: Correctness; no perf delta expected.

## HIP kernels

9. **PR #612**: HIP motion kernel. *Claim*: "Initial HIP motion implementation;
   equivalent to CUDA baseline (measured: parity with CUDA on AMD hardware)."
   Status: Measured.

10. **PR #675**: HIP CAMBI kernel. *Claim*: "CAMBI on HIP; GPU parity (measured:
    validated against CUDA baseline)." Status: Measured.

11. **PR #686**: HIP integer CAMBI. *Claim*: "Integer variant reduces register
    pressure; minor improvement expected (measured: ~1–2% on integer CAMBI)."
    Status: Measured.
