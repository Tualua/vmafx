# SYCL developer notes

This page is for contributors who add or change SYCL kernels. It holds the
source layout, the rules every kernel must follow (no fp64, no scratch memory,
sub-group size 16 or 32), and the compiler pitfalls that have bitten before.
Users of the backend need only the [SYCL overview](overview.md).

## Source layout

| Path | Role |
| --- | --- |
| `core/src/sycl/common.{cpp,h}` | SYCL queue, device selection, shared frame, profiling |
| `core/src/sycl/picture_sycl.{cpp,h}` | USM picture upload and the CPU path |
| `core/src/sycl/dmabuf_import.{cpp,h}` | Linux: zero-copy VA-API dmabuf import |
| `core/src/sycl/d3d11_import.cpp` | Windows: D3D11 staging-texture import |
| `core/src/sycl/dispatch_strategy.{cpp,h}` | `direct` / `graph` strategy selection |
| `core/src/sycl/scratch_check.{cpp,h}`, `scratch_ratchet.txt` | Scratch-memory probes and the ratchet list |
| `core/src/sycl/check_aot_image.py` | Build-time check that `libvmaf.so` holds every AOT image |
| `core/src/feature/sycl/*_sycl.cpp` | One file per extractor twin |
| `core/src/feature/sycl/sycl_*.h` | Shared device math: pairs, soft double, ordered sums, tile index, compat |

Per-subtree rebase invariants are in
[`core/src/sycl/AGENTS.md`](../../../core/src/sycl/AGENTS.md) and
[`core/src/feature/sycl/AGENTS.md`](../../../core/src/feature/sycl/AGENTS.md).

## Design notes

- **Single-source DPC++.** Kernels are ordinary C++ lambdas submitted through
  `queue::parallel_for`. There are no shader files and no separate SPIR-V
  assets: device code is compiled into the binary at build time, as native
  images for the [AOT targets](aot.md) plus a SPIR-V fallback.
- **Unified Shared Memory (USM).** The backend uses `malloc_device` for
  per-feature scratch buffers and a shared allocation for pictures when
  zero-copy is not available.
- **In-order queues per extractor.** Each feature extractor owns a SYCL
  in-order queue. The host-side dispatcher submits work without explicit event
  dependencies; the in-order semantics order the work inside an extractor.
- **One wait per frame.** The device-resident twins (CAMBI, SpEED, SSIMULACRA 2,
  MS-SSIM) enqueue everything in `submit()` and wait once in `collect()`. Do not
  bring back a mid-frame wait.

## `icpx` const-correctness on string default options

Per-extractor `VmafOption` rows declare `default_val.s` as `char *` (matching
the C API contract in `core/include/libvmaf/libvmaf.h`). The DPC++ compiler
(`icpx`) is stricter than g++ about C++ const-correctness and rejects
initializing a `char *` member from a `const char *` source. SYCL feature
kernels that need a string default should use `static char NAME[] = "..."`
(array decay) rather than `static constexpr const char *NAME = "..."`.

The CUDA twins use a `#define NAME "..."` macro for the same reason. Applies to
every `*_sycl.cpp` extractor that declares a string-typed default option.

## fp64-less device contract (T7-17)

All SYCL feature kernels run on devices that lack `sycl::aspect::fp64` (Intel
Arc A-series, most Intel iGPUs, many mobile and embedded GPUs). No kernel emits
double-precision SPIR-V instructions, so the JIT never falls back to int64
emulation and there is no per-kernel penalty on such devices.

How the twins meet the contract:

- **ADM gain limiting** uses 64-bit integer arithmetic
  (`adm_gain_limit_product()` in `core/src/feature/adm_gain_limit.h`). The CPU
  reference multiplies a 32-bit DWT coefficient by a `double` gain in
  `[1.0, 100.0]` and truncates the product toward zero. The device path returns
  that truncated double product exactly, from the gain's 53-bit significand and
  two 64-bit products, for the production values (`1.0`, `100.0`) and for
  non-integer gains alike
  ([ADR-1413](../../adr/1413-adm-gain-limit-truncated-double-product.md)).
- **VIF gain limiting** runs in fp32 (`sycl::fmin(g, vif_enhn_gain_limit)` over
  float operands). The host stores the gain as a `double` for parity with the
  CPU API, and the launcher casts to `float` before kernel submission.
- **Where the CPU computes in `double`**, the twin carries the value as an exact
  pair of floats (CIEDE, SpEED, `float_adm`) or computes it in 64-bit integers
  (`ssim`, `float_ssim`, `float_ms_ssim`, `ssimulacra2`). The sums are formed
  from integers or read back and added on the host. See [Twin notes](twins.md).

`VmafSyclState` records `has_fp64` at queue construction so a future fp64-only
optimisation can branch on it; current kernels do not. At
`VMAF_LOG_LEVEL_INFO` the init log confirms the path taken. On an fp64-less
device it reads "device lacks native fp64 — kernels already use fp32 + int64
paths, no emulation overhead". See
[ADR-0220](../../adr/0220-sycl-fp64-fallback.md).

!!! warning
    If a new SYCL kernel captures a `double` operand or calls
    `sycl::reduction<double>`, the Level Zero runtime rejects the entire
    SPIR-V module on Arc A-series, even when that kernel is never submitted.
    Audit the lambda capture list and every `sycl::reduce*` call before
    merging.

## Scratch memory: writing and checking kernels

Why scratch memory matters, what the start-up probes log, and how to run
`test_sycl_kernel_scratch` are on the
[overview](overview.md#scratch-memory-on-intel-gpus-adr-1395). This section is
the kernel author's side.

**Writing a kernel.** Keep arrays that are indexed at run time in local memory
or registers, and keep the live values within 128 registers per hardware thread
at the kernel's SIMD width. A small private array indexed by a value known only
at run time (`pixel.original[band]`) is enough to need scratch memory. Select
by value instead, as `fadm_load_cm_pixel()` in `float_adm_sycl.cpp` does.

When a kernel cannot stay within 128 registers, give it the 256-entry register
file with `VmafSyclKernelShape<SG, 256>` from
`core/src/feature/sycl/sycl_compat.h`, as `integer_vif_sycl.cpp` does.

Xe-LP integrated GPUs (`tgllp`, `adl-*`, `rpl-*`) have no 256-entry file and
spill at that size anyway. A kernel that fits them only at the SIMD-8 the
compiler picks there, while Xe2 accepts no sub-group below 16, takes
`VmafSyclKernelShape<0, 256>`: no required sub-group size, and the large file
where the target has one. The term kernel of `float_adm_sycl.cpp` does
([ADR-1501](../../adr/1501-sycl-float-adm-terms-large-grf-xe2.md)).

Then run `test_sycl_kernel_scratch` on an Intel GPU.

**Reading the build log.** The log of a default-list build (every target of
`sycl_icpx_aot_targets`) names each spill per target, which shows a spill on a
GPU you do not have:

```text
[bmg-g21] warning: in kernel '...launch_terms...': compiled SIMD32 allocated 128 regs and spilled around 2
```

The log does not show private arrays in memory; only the device test does.

## Sub-group sizes

A kernel may require only sub-group size 16 or 32. `sycl_compat.h` rejects any
other size at compile time, so a raw `[[sycl::reqd_sub_group_size(N)]]` or
`sub_group_size<N>` must not enter a kernel. The table of sizes per target
family and the two tests that stand in for the default build are in
[AOT targets](aot.md#sub-group-sizes-and-the-aot-targets-adr-1468).

## Forward declaration of `close_fex_sycl` (SY-2a)

Each `init_fex_sycl` in `core/src/feature/sycl/` calls `close_fex_sycl(fex)`
from its USM-allocation error paths, so that partial allocations and
`feature_name_dict` are released when init fails. The definition of
`close_fex_sycl` sits at the bottom of every translation unit, next to the
extractor's `VmafFeatureExtractor` registration struct. Each translation unit
therefore adds `static int close_fex_sycl(VmafFeatureExtractor *fex);` just
before its `init_fex_sycl`, which keeps strict C++ modes happy (icpx, MSVC,
clang `-Werror=implicit-function-declaration`). The same pattern applies to
`close_chroma_sycl` and `close_temporal_sycl` in `speed_chroma_sycl.cpp` and
`speed_temporal_sycl.cpp`.

## CAMBI histogram sizing and the SpEED twins (ADR-1179)

The SYCL CAMBI extractor supports `cambi_high_res_speedup` (`hrs`), which keeps
the feature dictionary key and the numerical result equal to the CPU's under the
default model `vmaf_v1.0.16_3d0h`. The CAMBI histogram buffers are sized by
`MAX(num_bins, v_band_size)`.

`speed_chroma_sycl` and `speed_temporal_sycl` use single-precision `float`
arithmetic and work-group accessors only, as ADR-0220 requires on hardware
without native double precision.
