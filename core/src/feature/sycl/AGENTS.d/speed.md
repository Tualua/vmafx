---
paths:
  - core/src/feature/sycl/speed_*_sycl.cpp
  - core/src/feature/sycl/speed_sycl_*
  - core/test/test_sycl_speed_*
invariant: SpEED pipeline arithmetic contract and singular-covariance contract; device-resident twins.
---
<!-- markdownlint-disable MD013 MD060 -->
# SpEED pipeline arithmetic and singular covariance

- **SpEED pipeline arithmetic contract ([ADR-1358](../../../../../docs/adr/1358-sycl-speed-device-resident-linalg.md)).**
  Every SpEED kernel lives in `speed_sycl_pipeline.cpp`; the two
  extractor TUs and `speed_sycl_host.cpp` hold none and never wait on
  the queue outside `pipeline_collect()` / `pipeline_wait()`. The four
  TUs are built with contraction off like every feature TU
  (`sycl_strict_fp_args`, ADR-1367). In the
  pipeline, every division and square root goes through `div_rn()` /
  `sqrt_rn()` (shared with ssimulacra2 in `sycl_exact_fp.h`, ADR-1363),
  every `log2f` through
  `speed_log2()`, every product feeding an add sits in a named
  temporary, and the fp64 comparisons of `speed.c` go through
  `below_eps()` / `below_eps_scaled()`. The file must not mention the
  fp64 type at all (`core/test/test_sycl_kernel_source_contract.py`).
  `lanczos4` prescale weights = host table, never a device sine: CPU
  rounds each weight once from fp64 `sin()`, `sycl::sinpi()` is ulps off
  and SpEED amplifies (5.8e-5 relative on an A380,
  `T-GPU-SPEED-LANCZOS4-PRESCALE-DRIFT-2026-09-30`). `upload_lanczos()`
  fills `Pipeline::lanczos` (device USM) at init from
  `speed_internal_gpu_lanczos_weights()`; `scale_lanczos()` reads `wx` /
  `wy` from it, so no private `wx[9]` / `wy[9]` (scratch, ADR-1395).
  Contract test plants a `sycl::sinpi`, a missing table, a private array;
  `test_sycl_speed_lanczos4_parity` = device check.
  On rebase: a plain `/` or `sycl::sqrt` added to a pipeline kernel, or
  a reduction reordered, breaks the bit-exact parity
  `test_sycl_speed_*_parity` measures; keep the order of every sum
  identical to its `speed.c` / `vif_tools.c` reference.

- **SpEED singular-covariance contract** — see canonical note in
  [`../cuda/AGENTS.md`](../../cuda/AGENTS.md). Since ADR-1358 the SYCL
  twins decide singularity on the device: `linalg_store()` in
  `speed_sycl_pipeline.cpp` writes the per-channel flag, and
  `block_statistics()` solves into a zero-initialised private solution
  that stays zero on a singular channel, so no device buffer is read
  before it is written. `score_group()` applies the one-sided rule and
  the flags reach the host in `FrameResult.singular`. ADR-1218.

| SYCL TU | CPU TU | Parity test | ADR |
|---|---|---|---|
| `speed_chroma_sycl.cpp` + `speed_sycl_pipeline.cpp` | `speed.c` | `test_sycl_speed_chroma_parity.c`, `test_sycl_speed_singular_parity.c` | ADR-0957 (round 4), ADR-1358 |
| `speed_temporal_sycl.cpp` + `speed_sycl_pipeline.cpp` | `speed.c` | `test_sycl_speed_temporal_parity.c`, `test_sycl_speed_singular_parity.c` | ADR-0957 (round 4), ADR-1358 |

> **SpEED twins are wired and device-resident (ADR-0964, ADR-1358).**
> Both extractors are in `sycl_feature_sources` with the shared
> `speed_sycl_pipeline.cpp` and `speed_sycl_host.cpp`. Their parity tests
> are live gates; on real video the twins match the CPU bit for bit (see
> `docs/metrics/speed_qa.md`).
