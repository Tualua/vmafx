# `vmaf_vpl` — Intel VPL zero-copy VAAPI→SYCL pipeline (developer tool)

`vmaf_vpl` is a developer-only binary that drives the libvmaf SYCL backend
through Intel's Video Processing Library (VPL) for zero-copy
VAAPI→SYCL frame transfer. It exists to exercise the
`libvmaf_sycl_dmabuf_import` path end-to-end against a real Intel GPU
without going through FFmpeg.

The tool is **built but not installed**. It only compiles when SYCL +
Intel VPL + libva + libva-drm are all present at configure time. The
canonical invocation is from the build tree
(`./build/core/tools/vmaf_vpl`). For most users the
[`vmaf_libvmaf_sycl` FFmpeg filter](ffmpeg.md#dedicated-sycl-filter-libvmaf_sycl)
is the right entry point; `vmaf_vpl` is for libvmaf SYCL contributors
debugging the import path itself.

## Build prerequisites

1. SYCL toolchain (Intel oneAPI 2025.3+ with `icpx`).
2. Intel VPL runtime (`libvpl-dev` on Ubuntu 24.04, or oneAPI bundle).
3. `libva-dev` + `libva-drm-dev` for the VAAPI surface input path.

If any of the above is missing, meson silently skips the
`vmaf_vpl` target — `meson setup build` will succeed without it and
the binary will not appear under `build/core/tools/`.

## Flags

| Flag | Type | Purpose |
| --- | --- | --- |
| `--ref PATH` | string | Reference video (any FFmpeg-decodable container; VPL decodes via VAAPI). |
| `--dis PATH` | string | Distorted video (same container constraints). |
| `--model PATH` | string | VMAF model JSON (e.g. `model/vmaf_v0.6.1.json`). |
| `--frames N` | uint | Number of frames to score (defaults to the full clip). |
| `--device N` | int | SYCL device index (use `--list-devices` first). |
| `--render-node PATH` | string | DRM render node (defaults to `/dev/dri/renderD128`). |
| `--fallback` | bool | Fall back to CPU upload (`vmaf_sycl_upload_plane`) when zero-copy import fails — useful for debugging. |
| `-h` / `--help` | — | Print the help text and exit. |

## Smoke invocation

```bash
./build/core/tools/vmaf_vpl \
  --ref testdata/ref_576x324_48f.yuv \
  --dis testdata/dis_576x324_48f.yuv \
  --model model/vmaf_v0.6.1.json \
  --frames 48 \
  --device 0
```

Successful runs print pooled VMAF on stdout and exit 0. Failed VAAPI
import surfaces a `vmaf_vpl: import failed: ...` message — re-run with
`--fallback` to confirm the SYCL backend itself is healthy and isolate
the issue to the import path.

## Status

The tool tracks ADR-0183 (FFmpeg `libvmaf_sycl` filter) — both share
the same SYCL dmabuf-import primitive. `vmaf_vpl` exists primarily as
a contributor regression-test entry point so the import path can be
debugged without an FFmpeg build round-trip. There is no plan to
install the binary; if you need a user-facing SYCL entry point use
the FFmpeg filter.

## Related

- [`ffmpeg.md`](ffmpeg.md) — FFmpeg `libvmaf_sycl` filter (the
  user-facing equivalent).
- [`docs/api/gpu.md`](../api/gpu.md) — `vmaf_sycl_dmabuf_import` C API.
- [ADR-0183](../adr/0183-ffmpeg-libvmaf-sycl-filter.md) — SYCL filter
  shipping policy.
