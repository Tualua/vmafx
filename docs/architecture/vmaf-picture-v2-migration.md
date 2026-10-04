<!-- markdownlint-disable MD060 -->
# `VmafPicture` v2 — consumer migration guide

> **Status:** v2 allocator and converters are implemented; no in-tree consumer
> uses v2 yet and `vmaf_read_pictures()` still takes v1 `VmafPicture`.
> The header is
> [`picture_v2.h`](../../core/include/libvmaf/picture_v2.h), the implementation
> is `core/src/picture_v2.c`, and the unit test is `test_picture_v2`
> (`core/test/test_picture_v2.c`, suite `fast`). The decision record and
> the cycle plan are in
> [ADR-0928](../adr/0928-vmaf-picture-v2-explicit-backend-state.md); the API
> reference is [Picture v2](../api/pictures.md#picture-v2-picture_v2h).

## Who needs this

Code that hands pictures to libvmaf from outside the library: an FFmpeg
filter, a language binding, or a tool that already knows which device owns
the pixels. If you only call `vmaf_picture_alloc()` and
`vmaf_read_pictures()` on CPU pictures, nothing changes for you today.

## Why v2 exists

The v1 struct (`core/include/libvmaf/picture.h`) carries a single `void *priv`
that the core and the backends overlay with their own records. A consumer
cannot inspect a `VmafPicture` and discover which backend owns its planes, so
passing a CUDA-backed picture to a SYCL extractor is silent undefined
behaviour rather than an explicit `-EINVAL`.

`VmafPicture2` keeps the v1 fields (`pix_fmt`, `bpc`, `w`, `h`, `stride`,
`data`, `ref`, `priv`) and adds `backend` (a `VmafBackendHandle`
discriminator: `NONE`, `CUDA`, `SYCL`, `HIP`, `METAL`; `VULKAN` is a reserved
value, the backend was removed in ADR-0726), `backend_handle` (a non-owning
stream or queue handle) and `_reserved[4]` for additive growth. The exact
declaration and the per-handle meaning of `backend_handle` are in
`picture_v2.h`.

## Using it today

CPU picture allocated as v2, or an existing v1 picture promoted:

```c
VmafPicture2 pic;
vmaf_picture2_alloc(&pic, VMAF_PIX_FMT_YUV420P, 8, 1920, 1080);
/* pic.backend == VMAF_BACKEND_HANDLE_NONE, pic.backend_handle == 0 */

VmafPicture v1;
vmaf_picture_v2_to_v1(&pic, &v1);   /* v1 view; takes a reference on pic */
vmaf_read_pictures(ctx, &v1, NULL, frame_index);
vmaf_picture_unref(&v1);
vmaf_picture2_unref(&pic);
```

There is no `vmaf_read_pictures2()`. The converters are
`vmaf_picture_v1_to_v2()` and `vmaf_picture_v2_to_v1()`; both increment the
source's reference count, so the caller releases both pictures. Plane layout
(`data[3]`, `stride[3]`), `VmafRef` refcounting and scores are identical
between v1 and v2.

## Migration plan (ADR-0928)

| Cycle | What ships | SONAME | v1 status |
|---|---|---|---|
| N | `picture_v2.h` declared; ADR-0928 | 3 | live default |
| N+1 | `picture_v2.c` implemented, header installed, `test_picture_v2` | 3 (additive) | live default |
| N+2 | In-tree backends, tools and FFmpeg patches switched to v2 | 3 | callable, deprecated |
| N+3 (target v4.0.0) | v1 entry points removed | 3 to 4 | removed |

The tree is at the end of cycle N+1. Cycle N+2 is not started: the FFmpeg
patches under `ffmpeg-patches/`, the MCP server and the Python harness do not
reference `VmafPicture2`. The ADR also plans a Rust binding that targets v2
from the start. When the FFmpeg patch stack changes for v2, it is refreshed in
the same PR as the library change
(`python3 scripts/ci/ffmpeg_patch_stack.py --refresh`, then `--check`; see
[FFmpeg patch automation](../development/ffmpeg-patch-automation.md) and the
[agent hard rules](../development/agent-hard-rules.md), rule 11).

## Cross-references

- [ADR-0928](../adr/0928-vmaf-picture-v2-explicit-backend-state.md) — decision
  record, alternatives, consequences.
- [ADR-0186](../adr/0186-vulkan-image-import-impl.md) — the hwaccel-import
  case that motivated typed handles (the Vulkan backend itself was removed,
  [ADR-0726](../adr/0726-drop-vulkan-backend.md)).
- `core/include/libvmaf/picture.h` — v1 header.
