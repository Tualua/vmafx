<!-- markdownlint-disable MD013 MD060 -->
# AGENTS.md — core/src/feature/sycl

Orientation for agents on per-feature SYCL kernels (DPC++).
Parent: [../AGENTS.md](../../AGENTS.md). Backend runtime (queue, USM,
dmabuf import) lives one level up in
[`../../sycl/AGENTS.md`](../../../sycl/AGENTS.md).

## Scope

```text
feature/sycl/
  <feature>_sycl.cpp           # one TU per kernel: registration + submit/collect + sycl::queue::submit lambda
```

All TUs compiled with `icpx` (Intel oneAPI) — build line
under [`../../meson.build`](../../../meson.build) adds `-fsycl` and the SYCL
strict FP line (`sycl_strict_fp_args`, ADR-1367) for every per-kernel TU.

## Ground rules

- **Parent rules** apply (see [../AGENTS.md](../../AGENTS.md) +
  [../../AGENTS.md](../../../AGENTS.md) +
  [`../../sycl/AGENTS.md`](../../../sycl/AGENTS.md)).
- **Wholly-new fork files use dual Netflix + Lusoris/Claude
  copyright header** per [ADR-0025](../../../../../docs/adr/0025-copyright-handling-dual-notice.md).
  Most TUs here fork-original SYCL ports of
  Netflix CUDA kernels.

## Twin-update rules

When a SYCL TU has a live CUDA, HIP, or Metal twin, user-visible behavior and
numeric fixes must be reviewed across those twins in the same PR. Vulkan was
removed in ADR-0726 and is not a live twin. The complete CUDA mapping lives in
[`../cuda/AGENTS.md`](../../cuda/AGENTS.md). The cross-backend parity gate at
`places=4`
([`scripts/ci/cross_backend_parity_gate.py`](../../../../../scripts/ci/cross_backend_parity_gate.py),
ADR-0214) catches drift only after a full GPU run; it does not replace that
source review.

## Rebase-sensitive invariants
