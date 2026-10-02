<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1468: A SYCL kernel requires only a sub-group size every default AOT target accepts (16 or 32); the six kernels that required 8 move to 16

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `build`, `ci`, `testing`, `rc3`, `fork-local`

## Context

The default build compiles every SYCL kernel ahead of time for 19 Intel GPU
targets (`sycl_icpx_aot_targets` in `core/meson_options.txt`,
[ADR-0568](0568-sycl-icpx-aot-targets-default.md),
[ADR-1360](1360-sycl-aot-compile-time-device-codegen.md)). The dev container
image uses that default. It has not built since `float_motion_sycl`'s row
kernel landed (#1703, 2026-10-01):

```text
FAILED: [code=1] src/float_motion_sycl.o
[lnl-m] error: in kernel '...launch_float_motion_row_sad...': Kernel compiled
with required subgroup size 8, which is unsupported on this platform
```

Reproduced on master `79d1089e6` without the container (icpx 2026.0, ocloc
26.35, `-Denable_sycl=true` with the option at its default, `ninja -k 0`).
Six translation units fail, each on one kernel, each for `lnl-m` (ocloc stops
at the first target that fails):

| Translation unit | Kernel |
|---|---|
| `core/src/feature/sycl/float_motion_sycl.cpp` | the row SAD (`launch_float_motion_row_sad`) |
| `core/src/feature/sycl/float_adm_sycl.cpp` | the row sums (`launch_row_sums`) |
| `core/src/feature/sycl/float_vif_sycl.cpp` | the row sums (`launch_vif_row_sums`) |
| `core/src/feature/sycl/ssimulacra2_sycl.cpp` | the sum walk (`Ss2TotalsKernel`) |
| `core/test/test_sycl_float_adm_math_probe.cpp` | the probe's row sums (`launch_scale`) |
| `core/test/test_sycl_ordered_sum_probe.cpp` | the probe's walk (`WalkKernel`) |

All six require sub-group size 8. No other kernel and no other kind of error
fails.

Which sizes a target accepts, measured with the installed ocloc on a one-line
OpenCL kernel with `intel_reqd_sub_group_size`:

| Targets | 8 | 16 | 32 |
|---|---|---|---|
| `tgllp`, `adl-s`, `adl-p`, `adl-n`, `rpl-s`, `rpl-p` (Xe-LP) | yes | yes | yes |
| `dg2-g10`, `dg2-g11`, `acm-g10`, `acm-g11`, `acm-g12` (Xe-HPG) | yes | yes | yes |
| `mtl-h`, `mtl-u`, `arl-h`, `arl-s`, `arl-u` (Xe-LPG) | yes | yes | yes |
| `lnl-m`, `bmg-g21`, `bmg-g31` (Xe2) | no | yes | yes |

(`ptl-h`, Xe3, not in the list: as Xe2.)

Nothing local saw the failure because a lane build is configured for one
device or with an empty target list (SPIR-V only), so it never compiles for
Xe2. Such a build has the same defect at run time: on an Xe2 device the
runtime compiler has to refuse the same kernels.

The six kernels asked for 8 for speed, not for correctness. Each runs a
sequential loop in one work-item (a row's sum, or one walk over a plane's
chunks), so its result does not depend on the sub-group size; narrow
sub-groups put more hardware threads on the memory reads
([ADR-1411](1411-sycl-float-motion-cpu-float-sum.md) measured 0.70 ms at 8, 0.82 at
16 and 1.12 at the compiler's choice for the `float_motion` row kernel on an
Arc A380).

## Decision

A SYCL kernel of the fork requires only a sub-group size that every target of
the default AOT list accepts: 16 or 32.

- The six kernels above require 16.
- `sycl_compat.h` checks the size at compile time
  (`VmafSyclSubGroupSize<N>`: a `static_assert` behind
  `VMAF_SYCL_REQD_SG_SIZE(N)` and `VmafSyclKernelShape<SG, GRF>`), so a
  build for one device, or a JIT-only build, rejects 8 too.
- `core/test/test_sycl_sub_group_size_contract.py` (suite `fast`, no compiler
  and no device) derives the allowed set from the option's default and the
  measured table, holds the header's check and every size the SYCL sources
  spell to it, and rejects a raw attribute or property that would pass the
  header by.
- `core/test/test_sycl_aot_default_targets.py` (suite `sycl-aot`, needs ocloc
  and no device) repeats the measurement of the table and, in a build that
  was not configured with the full default list, compiles every SYCL
  translation unit of the build for the full list again. It is the
  container's compile, run from a lane build.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| **One size every target accepts, 16 (chosen)** | One kernel per site, compiles everywhere, same bits; scratch-free and within 2 % on the A380 | The few per cent that 8 gained on Xe-HPG | — |
| A size per device family: 8 where accepted, 16 on Xe2 | Keeps the faster shape on Xe-LP / Xe-HPG | Two kernels per site and a choice at run time; the size-8 kernel still fails ahead-of-time compilation of its translation unit for Xe2, so it would need a unit of its own with its own target list (`tu_aot_list`) and no SPIR-V fallback for Xe2 | The gain is 0.1 ms per 4K frame at most |
| Drop the attribute (the compiler's choice) | Compiles everywhere | The choice is 32 on the A380 for these kernels (1.12 ms against 0.82 for the `float_motion` row kernel), and the fork pins sizes to keep kernels out of scratch memory ([ADR-1395](1395-sycl-kernels-no-scratch.md)): an unpinned kernel's spills differ per device | Slower, and not controlled |
| Take the Xe2 targets out of the default list, or list these units in `sycl_icpx_aot_igc_skip` | The build passes | The SPIR-V fallback then fails on an Xe2 device at run time with the same error | Moves the failure to the user |
| Only fix the kernels | Smallest change | The next size-8 kernel repeats this: every lane was green while the image did not build | The guard is the point |
| Run the full ahead-of-time compile in the `fast` suite | Fails where everyone looks | 35 units for 19 targets: minutes of ocloc per run | The compile-time check and the contract are the fast guard; the compile is `sycl-aot` |

## Consequences

- **Positive**: on the fixed tree the default-list build compiles all 35 SYCL
  translation units (27 of the library, 8 test probes) for all 19 targets;
  before, 6 failed.
- **Positive**: on an Arc A380 (xe) the four twins are unchanged: the gate
  reports 0 for `float_motion`, `float_adm`, `float_vif` and `ssimulacra2`
  on 333 of 333 frames of 14 fixtures at `--precision max`, their parity
  tests (`==`) and the two probes pass, and `test_sycl_kernel_scratch`
  audits 125 kernels: none uses scratch memory.
- **Positive**: a size no Xe2 target accepts is a compile error in every
  configuration, and a device-free test names the file.
- **Negative**: per 3840x2160 frame on the A380, medians of 7 interleaved
  runs: `float_motion_sycl` 4.27 ms instead of 4.18, `float_adm_sycl` 12.29
  instead of 12.26, `float_vif_sycl` 23.65 instead of 23.51,
  `ssimulacra2_sycl` 195.6 instead of 194.9; at 576x324 `float_vif_sycl`
  0.87 instead of 0.81 ms and the others within the noise of the runs.
- **Neutral / follow-ups**:
  - Not verified here, for lack of the devices: that the six kernels at size
    16 return the CPU's bits and use no scratch memory on Xe2 (Arc B580,
    Lunar Lake) and on Xe-LP / Xe-LPG integrated GPUs. They compile for all
    of them; their arithmetic does not depend on the size; scratch use
    depends on the device's register file. Before this change they did not
    compile for Xe2 at all.
    `T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02`.
  - The measured table is ocloc's answer for today's 19 targets. A target
    added to the list without an entry fails the contract until someone
    measures it.

## References

- `req` (coordinator brief, 2026-10-02, paraphrased): the dev container image has not built on master since the float_motion row kernel landed; reproduce with the default AOT list, fix every kernel for every listed target while keeping it scratch-free and bit-identical on the A380, and add a guard that fails on a Linux lane build.
- [ADR-0568](0568-sycl-icpx-aot-targets-default.md),
  [ADR-1360](1360-sycl-aot-compile-time-device-codegen.md) (the AOT build),
  [ADR-1395](1395-sycl-kernels-no-scratch.md) (no scratch memory),
  [ADR-1411](1411-sycl-float-motion-cpu-float-sum.md) (the row kernel and its
  measured shapes), [ADR-0407](0407-adaptivecpp-second-sycl-toolchain.md)
  (`sycl_compat.h`).
- `docs/state.md`: `T-SYCL-AOT-XE2-SUB-GROUP-SIZE-8-2026-10-02`,
  `T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02`.
