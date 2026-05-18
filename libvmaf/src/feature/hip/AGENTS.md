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

## HSACO symbol naming — kernel keys must match the host-TU consumer (ADR-0539)

When a HIP host TU references a kernel module via
`hipModuleLoadData(..., <name>_hsaco)`, the `hip_kernel_sources` meson
key MUST be exactly `<name>` — the `xxd -i -n <name>_hsaco` step inside
the meson custom_target derives the symbol from that key.  Two
gotchas:

1. **Distinct host TUs that consume different kernels must use
   distinct meson keys, even when the underlying `.hip` filename is
   `moment_score.hip`** for both.  Compare:

   ```meson
   # float_moment_hip.c consumes `moment_score_hsaco`
   'moment_score' : feature_src_dir + 'hip/float_moment/moment_score.hip',
   # integer_moment_hip.c consumes `integer_moment_score_hsaco`
   'integer_moment_score' : feature_src_dir + 'hip/integer_moment/moment_score.hip',
   ```

   The two `.hip` files contain different kernel entry points
   (`calculate_float_moment_*` vs `calculate_integer_moment_hip_kernel_*`)
   and are NOT interchangeable.

2. **A missing meson registration produces an undefined-reference link
   error** for `<name>_hsaco`, NOT a runtime `-ENOSYS`.  If you see
   such a link failure, either register the kernel (preferred) or add
   a weak stub in `hip_hsaco_stubs.c` (per ADR-0536 — only for kernels
   that can't yet compile standalone via `hipcc --genco`).
## Remove the weak HSACO stub the moment a real .hip lands (ADR-0539)

When a `.hip` kernel under `feature/hip/<extractor>/` becomes
standalone-buildable and you register it in `hip_kernel_sources` in
`libvmaf/src/meson.build`, **also delete its matching
`VMAF_HSACO_WEAK_STUB(<extractor>_score_hsaco)` line from
`hip_hsaco_stubs.c` in the same PR.**  Leaving the stub creates two
definitions of the same symbol — a strong xxd-embedded blob and a weak
1-byte fallback — which the linker resolves to the strong one but at
the cost of `-Wlto-type-mismatch` warnings on every build.  The user
direction is "no stubs anywhere" once a real kernel exists.

Pattern (ADR-0539 example for `float_vif_score`):
1. Confirm the `.hip` source compiles via `hipcc --genco` in the
   container (`ninja -C <build> src/<name>.hsaco`).
2. Remove the `VMAF_HSACO_WEAK_STUB(<name>_hsaco)` line from
   `hip_hsaco_stubs.c`.  Leave a one-line comment citing the ADR so the
   reviewer sees why the slot is gone.
3. Rebuild with `enable_hipcc=true` and grep the ninja output for
   warnings referencing the symbol — none should remain.
