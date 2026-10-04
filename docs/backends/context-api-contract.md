<!-- markdownlint-disable MD060 -->
# GPU Backend Context-API Contract

Backends that own a device context (HIP and Metal today) expose the same
three internal functions. Keeping one shape prevents per-backend API drift
and lets the feature-extractor glue stay uniform across backends. See
[ADR-0486](../adr/0486-context-api-contract-doc.md) for the decision record.

!!! note
    This is an internal contract between libvmaf's own translation units. The
    headers `core/src/<backend>/common.h` are not installed. The public,
    installed GPU surface is the state API in `libvmaf_cuda.h`,
    `libvmaf_sycl.h`, `libvmaf_hip.h` and `libvmaf_metal.h`; see
    [GPU API](../api/gpu.md).

## Which backends follow it

| Backend | Context layer | Notes |
|---------|---------------|-------|
| HIP | `vmaf_hip_context_*` in `core/src/hip/common.h` | follows the three-function shape |
| Metal | `vmaf_metal_context_*` in `core/src/metal/common.h` | follows the shape and adds two handle accessors |
| CUDA | `vmaf_cuda_state_init` / `vmaf_cuda_release` (public header) | exempt, predates the contract |
| SYCL | `vmaf_sycl_state_init` (public header) | exempt, uses a state API |

The CUDA and SYCL APIs are public and consumed by `ffmpeg-patches/`. Do not
rename them without a dedicated ADR and a matching update of the patches.

## Required functions

A backend named `<backend>` that follows the contract declares these in
`core/src/<backend>/common.h`. The Vulkan backend followed it before its
removal (ADR-0726).

```c
/* Allocate and initialise a new context bound to device_index.
 *
 * Returns  0            on success; *ctx is non-NULL.
 * Returns -EINVAL       if ctx is NULL or device_index is out of range.
 * Returns -ENODEV       if no device with that index exists.
 * Returns -ENOMEM       on allocation failure.
 *
 * The caller owns *ctx and must release it with vmaf_<backend>_context_destroy.
 */
int vmaf_<backend>_context_new(Vmaf<Backend>Context **ctx, int device_index);

/* Release all resources owned by ctx.
 *
 * Safe to call with ctx == NULL (no-op).
 */
void vmaf_<backend>_context_destroy(Vmaf<Backend>Context *ctx);

/* Return the number of available devices of this type.
 *
 * Returns >= 0          number of devices (0 means "none found, but
 *                       device discovery succeeded").
 * Returns -ENODEV       if device discovery itself failed.
 */
int vmaf_<backend>_device_count(void);
```

## Error-return contract

| Condition                         | Return value |
|-----------------------------------|--------------|
| Success                           | `0`          |
| `ctx` or required pointer is NULL | `-EINVAL`    |
| Device index out of range         | `-EINVAL`    |
| No device found                   | `-ENODEV`    |
| Allocation failure                | `-ENOMEM`    |
| Runtime / driver error            | `-EIO` (fallback; prefer a more specific errno where the underlying runtime maps cleanly) |

`context_destroy` never returns an error because it is `void`. A cleanup error
that cannot be handled silently is logged through `vmaf_log` before it is
discarded.

### Where the implementations deviate

The Metal context implements the table (`core/src/metal/common.mm` returns
`-EINVAL` for a NULL out-pointer and `-ENODEV` when the device index does not
match a supported GPU). The HIP context does not yet:

- `vmaf_hip_context_new` only rejects a NULL out-pointer and never inspects
  `device_index`, so it returns neither `-EINVAL` nor `-ENODEV` for a bad
  index (`core/src/hip/common.c`).
- `vmaf_hip_device_count` returns `0` when `hipGetDeviceCount` fails, not
  `-ENODEV`. The callers branch on `count > 0`.

These are code gaps against the contract, not documented behaviour.

## Opaque-handle accessors (optional)

Where a backend context owns GPU handles that consumer translation units need
(for example a queue or a device handle), expose them through typed accessors:

```c
/* Returns the opaque handle; NULL for a NULL ctx.
 * Lifetime: tied to ctx — callers must NOT release the returned pointer. */
void *vmaf_<backend>_context_<handle_name>(Vmaf<Backend>Context *ctx);
```

Metal is the reference: `vmaf_metal_context_device_handle` and
`vmaf_metal_context_queue_handle` (ADR-0361). The accessor returns an opaque
pointer so pure-C consumers never depend on the struct layout.

## Checklist for new backends

- [ ] `vmaf_<backend>_context_new` is declared in `core/src/<backend>/common.h`.
- [ ] `vmaf_<backend>_context_destroy` accepts NULL without crashing.
- [ ] `vmaf_<backend>_device_count` returns `>= 0` on success, `-ENODEV` on
      discovery failure.
- [ ] `vmaf_<backend>_context_new` validates `device_index` and returns
      `-EINVAL` / `-ENODEV` as in the table above.
- [ ] Error returns use POSIX errno values (not raw runtime codes).
- [ ] Opaque-handle accessors follow the `void *` + lifetime-tied pattern.
- [ ] The public state API (`libvmaf_<backend>.h`) is installed through
      `core/include/libvmaf/meson.build`.
