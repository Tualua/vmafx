---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_ssim_hip.c
invariant: Static const lookup tables must be uploaded to device memory before kernel launch.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Static const tables must be uploaded to device memory (ADR-0537)

Host-side `static const` array (e.g. `vif_filter1d_table[4][18]`
from `feature/integer_vif.h`) needing to be readable from HIP kernel
-> allocate device buffer at init time, `hipMemcpy(...,
hipMemcpyHostToDevice)` table contents once. Don't try passing host
address into kernel via `args[]` — WILL fault.

Cost = ~150 bytes one-shot at init, amortised across extractor's
lifetime. Established precedent: ADR-0537 in
`integer_vif_hip.c::init_fex_hip()`.
