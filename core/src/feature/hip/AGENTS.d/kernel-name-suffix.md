---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_vif_hip.c
invariant: Kernel name suffixes do not encode filter half-width dimensions.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Kernel name-suffix convention does NOT encode filter half-width (ADR-0537)

CUDA-port kernel-name suffixes like
`filter1d_8_vertical_kernel_uint32_t_17_9` or
`filter1d_16_vertical_kernel_uint2_3_0_3` encode
`(fwidth_0, fwidth_1, scale)` — *full filter widths* for main filter and rd
downsample filter, plus scale index. NOT half-widths.

Correct filter half-widths come from `vif_filter1d_width[scale] / 2`:

| Scale | `fwidth` | `half_width` |
|-------|----------|--------------|
| 0     | 17       | 8            |
| 1     | 9        | 4            |
| 2     | 5        | 2            |
| 3     | 3        | 1            |

Pre-ADR-0537 `integer_vif/vif_statistics.hip` used
`HALF = 9 / 5 / 3 / 0` (parsed from suffix), read 19 / 11 / 7 / 1 filter coefficients
per output pixel from 18-entry table — out-of-bounds reads.
