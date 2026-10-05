# GPU Backend Public-API Template

This page is the recipe for the public C header of a new GPU backend
(DirectML, OpenCL, a future ROCm replacement, ...). Follow the shape the
existing four backends (`libvmaf_cuda.h`, `libvmaf_sycl.h`, `libvmaf_hip.h`,
`libvmaf_metal.h`) already share.

!!! note
    There is **no codegen**; the dedup-via-pattern lives here. A 2026-05-02
    audit measured the four headers at about 20 of about 200 lines truly
    shared (state lifecycle). The rest is backend-specific feature surface
    (CUDA: preallocation; SYCL: DMABuf / VA-surface / D3D11; HIP: ROCm
    kernels; Metal: IOSurface import). Codegenning 10 % of each file would add
    a build-system Python dependency for too little return. ADR-0239 chose
    pattern-doc over codegen.

## Files a new backend adds

Work through this checklist; each item is detailed below or in the
[add-gpu-backend skill](../../.claude/skills/add-gpu-backend/SKILL.md).

1. A public header `core/include/libvmaf/libvmaf_<backend>.h` with the
   [shared lifecycle](#shared-lifecycle).
2. Optional header surfaces, only when the backend needs them:
   [device enumeration](#optional-device-enumeration),
   [availability probe](#optional-build-time-availability-probe),
   [picture preallocation](#optional-picture-preallocation-surface-cuda-and-sycl),
   [zero-copy import](#optional-zero-copy-hwaccel-import-paths).
3. The backend's internal files under `core/src/<backend>/`, see
   [Internal-side companion files](#internal-side-companion-files-not-in-this-header).
4. A Meson option, per-feature kernels under `core/src/feature/<backend>/`,
   and a CI workflow.
5. Documentation under `docs/backends/` in the same PR (hard rule 7 in
   [agent-hard-rules.md](agent-hard-rules.md)).
6. A matching `ffmpeg-patches/` update when the public ABI changes, see
   [Doxygen and ABI stability conventions](#doxygen-and-abi-stability-conventions).

## Shared lifecycle

Every GPU backend ships these three entry points and the state types:

```c
typedef struct Vmaf<Backend>State Vmaf<Backend>State;

typedef struct Vmaf<Backend>Configuration {
    int device_index;            /* -1 = first compatible device */
    /* Backend-specific config fields here. Keep additions additive
     * (zero-initialised structs must compile + run correctly). */
} Vmaf<Backend>Configuration;

/**
 * Allocate a Vmaf<Backend>State. Picks the device by index; -1 selects
 * the first device that exposes the required compute/queue capability.
 *
 * @return 0 on success, -ENOSYS when built without <Backend> support,
 *         -ENODEV when no compatible device is found, -EINVAL on bad
 *         arguments.
 */
int vmaf_<backend>_state_init(Vmaf<Backend>State **out, Vmaf<Backend>Configuration cfg);

/**
 * Hand the state to a VmafContext. The context borrows the state pointer
 * through teardown; the caller still owns the state. Keep it alive across
 * every nonzero vmaf_close() result and free it only after close returns 0.
 */
int vmaf_<backend>_import_state(VmafContext *ctx, Vmaf<Backend>State *state);

/**
 * Release a state previously allocated via vmaf_<backend>_state_init.
 * Safe to pass NULL or a state that was never imported.
 *
 * Two valid signatures exist across the existing four backends:
 *
 *     void vmaf_<backend>_state_free(Vmaf<Backend>State **state);   // SYCL, HIP, Metal
 *     int  vmaf_<backend>_state_free(Vmaf<Backend>State *state);    // CUDA
 *
 * Pick the void/double-pointer form for new backends — it lets the
 * function NULL the caller's pointer (CUDA's int-return form is a
 * historical quirk inherited from upstream Netflix and exists in
 * fewer call sites; new backends should not replicate it).
 */
void vmaf_<backend>_state_free(Vmaf<Backend>State **state);
```

## Optional: device enumeration

Backends whose device set is dynamic or runtime-detected (SYCL, HIP, Metal)
ship a `_list_devices` helper that prints one line per device with ordinal,
name and capability. Backends without a public enumeration contract skip this.

```c
/**
 * Enumerate compute-capable devices visible to the runtime. Prints
 * one line per device with its ordinal, name, and capability.
 * @return device count, or -ENOSYS when built without <Backend> support.
 */
int vmaf_<backend>_list_devices(void);
```

## Optional: build-time availability probe

Backends conditionally compiled through a Meson option ship an `_available()`
query, so callers can branch on backend presence without linking against the
symbol table directly. HIP and Metal ship it today.

```c
/**
 * Returns 1 if libvmaf was built with <Backend> support, 0 otherwise.
 * Cheap to call; no <Backend> runtime is touched until
 * vmaf_<backend>_state_init().
 */
int vmaf_<backend>_available(void);
```

## Optional: picture preallocation surface (CUDA and SYCL)

When the backend wants callers to write directly into the buffers the kernel
will read (avoiding a host-to-device staging copy), expose the preallocation
pool. The shape mirrors the SYCL surface, the cleaner of the two. CUDA's
`HOST_PINNED` is specific to the CUDA allocator and has no analogue
elsewhere, so do not replicate it.

```c
enum Vmaf<Backend>PicturePreallocationMethod {
    VMAF_<BACKEND>_PICTURE_PREALLOCATION_METHOD_NONE = 0,
    VMAF_<BACKEND>_PICTURE_PREALLOCATION_METHOD_HOST,
    VMAF_<BACKEND>_PICTURE_PREALLOCATION_METHOD_DEVICE,
};

typedef struct Vmaf<Backend>PictureConfiguration {
    struct {
        unsigned w, h;
        unsigned bpc;
        enum VmafPixelFormat pix_fmt;
    } pic_params;
    enum Vmaf<Backend>PicturePreallocationMethod pic_prealloc_method;
} Vmaf<Backend>PictureConfiguration;

int vmaf_<backend>_preallocate_pictures(VmafContext *vmaf,
                                         Vmaf<Backend>PictureConfiguration cfg);
int vmaf_<backend>_picture_fetch(VmafContext *vmaf, VmafPicture *pic);
```

!!! note
    The fetch entry point is named differently today: SYCL uses
    `vmaf_sycl_picture_fetch`, CUDA uses
    `vmaf_cuda_fetch_preallocated_picture`. New backends use the SYCL name.

The implementation MUST delegate to the backend-agnostic `VmafGpuPicturePool`
(`core/src/gpu_picture_pool.{h,cpp}`) per ADR-0239. Do not reimplement the
round-robin / mutex / unwind shape. Each backend supplies the alloc, free and
synchronize callbacks and a per-pool cookie carrying its state pointer.

## Optional: zero-copy hwaccel import paths

Some backends can adopt externally decoded GPU memory: SYCL (DMABuf,
VA-surface, D3D11) and Metal (IOSurface). Each ships a separate import
surface that varies enough per backend that no pattern is forced; design it
backend-natively. Document the lifetime model: who owns the source handle, who
owns the imported state, and when it is safe to free the source.

## Doxygen and ABI stability conventions

- Every public function carries a Doxygen block with `@return` listing every
  error code path, including `-ENOSYS` for the built-without-backend case.
- Configuration structs grow **additive only**. Zero-initialised structs from
  older callers must keep compiling and running with default behaviour. This
  is enforced project-wide; new fields go at the end of the struct, never in
  the middle.
- Opaque state types (`Vmaf<Backend>State`) are forward-declared in the public
  header. Their layout lives in a backend-internal header or source file (see
  the table below), so kernel translation units can read the device, queue and
  allocator handles without crossing the public surface.
- Public ABI changes (renames, removed entry points, signature shape changes)
  are forbidden without an ADR and a matching `ffmpeg-patches/` update (hard
  rule 11 in [agent-hard-rules.md](agent-hard-rules.md); see also
  [FFmpeg patch automation](ffmpeg-patch-automation.md)).

## Internal-side companion files (NOT in this header)

The backend-internal files follow their own pattern under
`core/src/<backend>/`. Files that exist today:

| Backend | Files |
| --- | --- |
| CUDA | `common.{c,h}`, `picture_cuda.{c,h}`, `dispatch_strategy.{c,h}`, `kernel_template.h`, `drain_batch.{c,h}`, `cuda_helper.cuh` |
| HIP | `common.{c,h}`, `picture_hip.{c,h}`, `dispatch_strategy.{c,h}`, `kernel_template.{c,h}`, `hip_handle.h`, `shared_frame.{c,h}`, `stubs.c` |
| Metal | `common.{h,mm}`, `picture_metal.{h,mm}`, `picture_import.mm`, `import.h`, `dispatch_strategy.{c,h}`, `kernel_template.{h,mm}`, `state_priv.h`, `stubs.c` |
| SYCL | `common.{cpp,h}`, `picture_sycl.{cpp,h}`, `dispatch_strategy.{cpp,h}`, `dmabuf_import.{cpp,h}`, `d3d11_import.cpp` |

The roles:

- `common.*`: state init, device enumeration and queue setup. The opaque
  state layout lives here for CUDA (`common.h`) and SYCL (`common.cpp`), in
  `hip_handle.h` for HIP and in `state_priv.h` for Metal. There is no
  `<backend>_internal.h`.
- `picture_<backend>.*`: buffer and picture allocation.
- `dispatch_strategy.*`: per-feature dispatch helpers.
- `kernel_template.*`: per-backend scaffolding for the lifecycle every
  feature kernel repeats (streams, events, partial-init unwind), introduced by
  [ADR-0246](../adr/0246-gpu-kernel-template.md). CUDA, HIP and Metal have
  one; SYCL has none.

The `gpu_picture_pool.{cpp,h}` round-robin is **shared**: every backend that
wants a preallocation pool delegates to it (ADR-0239).

The feature kernel host glue, which every `<feature>_<backend>.c` under
`core/src/feature/<backend>/` ships, can build on the backend's
`kernel_template` (for example `integer_motion_v2_cuda.c` includes the CUDA
one).

## Row addressing above 8 bits

A `VmafPicture` row stride, a pitch from the device allocator and every stride
the twins pass to a kernel count bytes. Above 8 bits a sample is two bytes, so
address a row through a byte pointer and cast the row:

```c
const uint16_t *row = reinterpret_cast<const uint16_t *>(plane + y * stride);
```

A `uint16_t *` advanced by `y * stride` reads row `2 * y` and, for the lower
half of the picture, memory past the plane. A stride that counts elements is
converted in the expression (`stride / sizeof(T)`) or named for its unit
(`stride_elems`). `core/test/test_gpu_byte_stride_contract.py` (fast suite)
scans every CUDA, HIP, SYCL and Metal source under `core/src` and fails on the
byte-stride form.

To check device memory accesses on CUDA, run a build's CUDA tests under
compute-sanitizer:

```bash
python3 scripts/ci/run_meson_test.py -- -C build --suite gpu -t 20 \
    --wrapper '/opt/cuda/bin/compute-sanitizer --tool memcheck --error-exitcode 9'
```

Device-free tests exit 255 under the sanitizer (no CUDA call to instrument),
and the out-of-memory tests report the `CUDA_ERROR_OUT_OF_MEMORY` they provoke.
HIP has an equivalent only on `xnack+` targets: ROCm's GPU AddressSanitizer
instruments device code for those targets alone, so it cannot check a
device without XNACK, such as a gfx1036.

## See also

- [ADR-0239](../adr/0239-gpu-picture-pool-dedup.md): backend-agnostic GPU
  picture pool (PR2 of the dedup sequence).
- [ADR-0246](../adr/0246-gpu-kernel-template.md): per-backend kernel
  scaffolding templates.
- [ADR-0250](../adr/0250-tiny-ai-extractor-template.md): tiny-AI extractor
  template (the model for "pattern-doc + shared helpers rather than codegen").
- [`core/include/libvmaf/AGENTS.md`](../../core/include/libvmaf/AGENTS.md):
  the public-headers-tree invariant note that points back here.
