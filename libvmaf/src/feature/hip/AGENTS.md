# HIP Feature Extractors — Invariant Notes

## Memory copy direction enum discipline

Every `hipMemcpy*` call's direction enum **must match the actual memory placement** of source and destination pointers:

- `hipMemcpyHostToDevice`: source is host-accessible (CPU pointer), destination is device-side
- `hipMemcpyDeviceToHost`: source is device-side, destination is host-accessible (CPU or pinned)
- `hipMemcpyDeviceToDevice`: source and destination are both device-side

Mismatches are undefined behavior on some HIP runtimes and may silently corrupt results or trigger runtime faults.

**Established patterns:**
- Picture planes arrive from the VMAF pipeline as CPU-side `VmafPicture` structs with `data[0..2]` pointers (host memory). Copying these into device-allocated staging buffers requires `hipMemcpyHostToDevice`.
- Readback buffers allocated via `hipHostMalloc` in `src/hip/kernel_template.c` are host-pinned memory, safe to use with `hipMemcpyDeviceToHost` for kernel output collection.

See PR #[TBD] / ADR-[TBD] for the discovery and fix of `integer_psnr_hip.c` lines 316/322 (2026-05-16 GPU audit).

## Kernel-arg pattern for `hipModuleLaunchKernel` pointer parameters (ADR-0537)

When a `__global__` kernel takes a pointer parameter (e.g.
`const uint16_t *vif_filt_dev`), the corresponding entry in the host
`void *args[]` array must be the **address of a variable that holds
the device pointer** — NOT the device pointer value itself, and NOT
the address of host memory.

```c
/* CORRECT — &dev_ptr_var points to the variable storing the device ptr */
void *dev_ptr = s->some_dev_malloc;
void *args[] = { /* …, */ &dev_ptr, /* … */ };
hipModuleLaunchKernel(func, …, args, NULL);

/* WRONG — passes a host address that the GPU will dereference */
void *args[] = { /* …, */ (void *)host_static_array, /* … */ };

/* WRONG — passes the pointer value into the position where the HIP
 * runtime expects the address-of-pointer */
void *args[] = { /* …, */ s->some_dev_malloc, /* … */ };
```

The pre-ADR-0537 `integer_vif_hip.c` had the second form for the
filter table parameter, which the AMD GPU dereferenced and faulted
on with "Memory access fault by GPU node-1 ... Reason: Page not
present or supervisor privilege" — on the first frame, before any
score had been produced.

## Static const tables must be uploaded to device memory (ADR-0537)

If a host-side `static const` array (e.g. `vif_filter1d_table[4][18]`
from `feature/integer_vif.h`) needs to be readable from a HIP kernel,
allocate a device buffer at init time and `hipMemcpy(...,
hipMemcpyHostToDevice)` the table contents once.  Don't try to pass
the host address into the kernel via `args[]` — it WILL fault.

The cost is ~150 bytes one-shot at init, amortised across the
extractor's lifetime.  Established precedent: ADR-0537 in
`integer_vif_hip.c::init_fex_hip()`.

## Kernel name-suffix convention does NOT encode filter half-width (ADR-0537)

The CUDA-port kernel-name suffixes like `filter1d_8_vertical_kernel_uint32_t_17_9`
or `filter1d_16_vertical_kernel_uint2_3_0_3` encode `(fwidth_0, fwidth_1, scale)`
— the *full filter widths* for the main filter and the rd downsample filter,
plus the scale index.  They are NOT half-widths.

Correct filter half-widths come from `vif_filter1d_width[scale] / 2`:

| Scale | `fwidth` | `half_width` |
|-------|----------|--------------|
| 0     | 17       | 8            |
| 1     | 9        | 4            |
| 2     | 5        | 2            |
| 3     | 3        | 1            |

The pre-ADR-0537 `integer_vif/vif_statistics.hip` used `HALF = 9 / 5 / 3 / 0`
(parsed from the suffix), which read 19 / 11 / 7 / 1 filter coefficients per
output pixel from an 18-entry table — out-of-bounds reads.

## Scalar-per-thread is the correctness baseline; templated tiled is the perf goal

When porting a CUDA twin to HIP, write the kernel scalar-per-thread first
(no shared-memory tiling, no warp reductions) and confirm cross-backend
parity at `places=3` (or better) on the Netflix golden pair *before*
porting the perf optimisations.  HIP wavefront sizes differ between
RDNA (32) and GCN/CDNA (64), so the warp-reduce path needs its own tuning
even after the scalar kernel is bit-exact.

Established precedent: ADR-0537 ports `integer_vif/vif_statistics.hip`
scalar-per-thread (~540 lines vs the CUDA twin's ~850), accepts a
~5–10× perf regression vs CUDA in exchange for a verifiable kernel
surface.  Perf optimisation deferred to a follow-up ADR.
