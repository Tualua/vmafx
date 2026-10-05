<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1830: `vif_sycl` runs at SIMD-16 only; the SIMD-32 kernels and `VMAF_SYCL_VIF_SUBGROUP_SIZE` are removed

- **Status**: Accepted (amends [ADR-1395](1395-sycl-kernels-no-scratch.md))
- **Date**: 2026-10-05
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu`, `testing`, `rc3`, `fork-local`

## Context

`vif_sycl` (`core/src/feature/sycl/integer_vif_sycl.cpp`) carried every
horizontal and fused kernel twice, at sub-group size 16 and 32. The runtime
picks SIMD-16 on every Intel GPU, so the SIMD-32 instances ran only when
`VMAF_SYCL_VIF_SUBGROUP_SIZE=32` forced them (`test_sycl_vif_parity_sg32`
did). [ADR-1395](1395-sycl-kernels-no-scratch.md) gave them the 256-entry
register file (`VmafSyclKernelShape<32, 256>`) because at the default 128
registers they spilled on an Arc A380, and a kernel that spills returns wrong
values on Arc A-series under the xe driver.

Two tester reports from UHD 770 integrated GPUs (Xe-LP; issue #2116 and the
office report of #2122) showed that Xe-LP has no 256-entry register file:
there the eight SIMD-32 instances compile at 128 registers and spill 1664 to
10048 bytes per thread (ocloc `.ze_info` for `tgllp`, `adl-*` and `rpl-*`
agrees), so `test_sycl_kernel_scratch`, which reads every kernel in the
binary, fails on that GPU whether or not the kernels run. No one shape keeps
SIMD-32 out of scratch on both Xe-LP and the A380.

SIMD-32 was never faster. On an Arc A380 at 3840x2160 (BBB, (t(35) - t(5)) /
30, medians of 3), forced SIMD-32 took 21.19 ms per frame against 21.20 ms for
the default SIMD-16 with separate kernels, and 33.85 ms against 23.50 ms with
`vif_fused=true`.

## Decision

`vif_sycl` has one sub-group size, `VIF_SG_SIZE = 16`. The SIMD-32 template
instances, their dispatchers, the `use_simd16` state and the
`VMAF_SYCL_VIF_SUBGROUP_SIZE` override are removed, as is
`test_sycl_vif_parity_sg32`. A device without SIMD-16 sub-groups (no Intel GPU)
is refused in `init()` with `-ENOTSUP` before anything is allocated. The
scale-0 horizontal kernel keeps the shape of
[ADR-1501](1501-sycl-float-adm-terms-large-grf-xe2.md) (no required size,
256 registers) and the scale-0 fused kernel keeps `<16, 256>`; the others are
`<16, 0>`. Measured with ocloc 26.35 / IGC 2.41.5 for the 19 default targets,
no kernel of `integer_vif_sycl.cpp` uses scratch memory on any of them.
`test_sycl_kernel_source_contract.py` refuses a SIMD-32 vif path (the
variable, `use_simd16`, a second template parameter, a `<32` shape or a
`, 32>(` instantiation).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Drop the SIMD-32 kernels and the override (chosen) | One shape, scratch-free on all 19 targets; nothing to dispatch per device; no A380 cost (SIMD-16 is the default and was never slower) | Removes a diagnostic switch a user might have set; a SYCL device without SIMD-16 can no longer run `vif_sycl` | — |
| Keep them; refuse the override and skip them in the audit on a device without the 256-entry register file | Keeps the switch | A per-device rule in the runtime and the audit for kernels nobody runs by default; the audit no longer reads every kernel | More code to keep a slower path |
| Reshape the SIMD-32 kernels to fit 128 registers | Keeps the switch on every device | Not attempted; A380 cost unknown; a reshaped SIMD-32 kernel has to stay exact and scratch-free on every target | Effort for a path that was never faster |

## Consequences

- **Positive**: the UHD 770 scratch audit can pass once the other four Xe-LP
  kernels are fixed (#2150); `vif_sycl` loses eight kernels and about 60 lines
  of dispatch.
- **Negative**: `VMAF_SYCL_VIF_SUBGROUP_SIZE` is gone; setting it now does
  nothing (libvmaf no longer reads it). A non-Intel SYCL device without
  SIMD-16 gets `-ENOTSUP` from `vif_sycl`.
- **Neutral / follow-ups**: docs (`docs/backends/sycl/overview.md`,
  `docs/usage/env-vars.md`) and the SYCL agent notes drop the variable; the
  UHD 770 re-run of the Intel GPU tester image is the device evidence for
  `T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02`.

## References

- `Q1.1` (maintainer popup answer, 2026-10-05, verbatim): "Drop the SIMD-32 kernels (Recommended)".
- [ADR-1395](1395-sycl-kernels-no-scratch.md) (no scratch memory; its "First fix" bullet is amended here),
  [ADR-1468](1468-sycl-sub-group-sizes-every-aot-target.md) (sub-group sizes on every AOT target),
  [ADR-1501](1501-sycl-float-adm-terms-large-grf-xe2.md) (no required size with 256 registers).
- `docs/state.md`: `T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02`.
- Hardware reports: issue #2116 (i5-13500, UHD 770), #2122 (i9-12900K, UHD 770).
