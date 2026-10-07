# SYCL zero-copy imports and picture pre-allocation

This page is for callers that hand decoded frames to the SYCL backend: the
FFmpeg `libvmaf_sycl` filter, or a program of your own that owns a decoder. It
says which ingestion path to use, the two rules that keep 10-bit QSV input
correct, and how the pre-allocated picture pool works. Back to the
[SYCL overview](overview.md).

## Ingestion paths

| Path | Entry point | Platform | Copies |
| --- | --- | --- | --- |
| Host pictures | `vmaf_read_pictures()` | all | One upload of both luma planes per frame into the state's shared frame |
| Pre-allocated pool | `vmaf_sycl_preallocate_pictures()`, `vmaf_sycl_picture_fetch()` | all | None when the decoder writes device USM |
| VA-API dmabuf | `vmaf_sycl_dmabuf_import()`, `vmaf_sycl_import_va_surface()` | Linux | None (zero-copy) |
| D3D11 staging | `vmaf_sycl_import_d3d11_surface()` | Windows | Two PCIe copies; not zero-copy |
| Plane upload | `vmaf_sycl_upload_plane()` | all | One host-to-device copy |

The signatures are in
[`libvmaf_sycl.h`](../../../core/include/libvmaf/libvmaf_sycl.h) and the
programmatic surface in [api/gpu.md](../../api/gpu.md#sycl).

## VA-API dmabuf import

When the input is a VA-API surface, for example from a QSV-decoded FFmpeg
frame, the backend imports the dmabuf directly through
`ext::oneapi::experimental::external_memory`, with no CPU upload. See
[`dmabuf_import.cpp`](../../../core/src/sycl/dmabuf_import.cpp).

The fast path is Linux-only. It is gated on `#ifndef _WIN32`, and on Windows
`vmaf_sycl_dmabuf_import` and `vmaf_sycl_import_va_surface` return `-ENOSYS`
so the caller falls back to the D3D11 staging path. DMA-BUF is a Linux kernel
interface (`ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF`); Level Zero on Windows uses
NT handles instead.

The zero-copy import (FFmpeg `libvmaf_sycl` with QSV surfaces, or any
caller of `vmaf_read_pictures_sycl()`) imports luma and, for 4:2:0 NV12 and
P010 surfaces, the Cb and Cr planes on every frame
([ADR-1597](../../adr/1597-sycl-zerocopy-planar-chroma-import.md)), and hands
the extractors no host pictures. Every SYCL extractor runs on it and scores
from the imported planes
([ADR-1598](../../adr/1598-sycl-host-staging-to-shared-planes.md),
[ADR-1599](../../adr/1599-sycl-float-motion-add-uv.md)): `adm_sycl`,
`cambi_sycl`, `float_moment_sycl`, `motion_sycl` and `float_motion_sycl`
(also with `motion_add_uv=true`), `motion_v2_sycl`, `vif_sycl`, `psnr_sycl`
and `psnr_hvs_sycl` (luma and chroma), `float_psnr_sycl`, `float_adm_sycl`,
`float_vif_sycl`, `integer_ssim_sycl`, `float_ssim_sycl`,
`float_ms_ssim_sycl`, `ciede_sycl`, `ssimulacra2_sycl`, `speed_chroma_sycl`
and `speed_temporal_sycl`. Both `vmaf_v0.6.1` and `vmaf_float_v0.6.1` score
on it. On an Arc A380 the full FFmpeg harness (`pass=50 fail=0 nonexact=0`,
8-bit NV12 and 10-bit P010) finds every value equal to the CPU's, or, for
`motion_add_uv` (the integer CPU `motion` has no such option), equal to the
host-upload run of the same twin. Two cases are still refused with
`-ENOTSUP` (error number 95 on Linux), before the frame changes any state
and never as a skip or a score from stale data: a CPU extractor (it needs a
host picture and zero-copy has none), and a chroma reader on an import that
carried no chroma. Two messages name the cause:

- A CPU extractor in the context:
  `vmaf_read_pictures_sycl: feature extractor '<name>' runs on the CPU and
  needs host pictures, which zero-copy input does not provide; register
  its SYCL twin '<twin>' instead` (or `; it has no SYCL twin`).
- A chroma reader on an import that carried no chroma, prefixed with the
  extractor's name:
  `needs chroma planes, which this zero-copy import did not provide`.
  The D3D11 import (Windows) is luma only, so `psnr` / `psnr_hvs` with
  chroma, `motion_add_uv` on either motion twin, `float_ms_ssim` with
  `enable_chroma=true`, and `ciede`, `ssimulacra2` and `speed_chroma` (which
  always read chroma) fail with this message there; set `enable_chroma=false`
  where the option exists to score luma only. Chroma import for D3D11 is out
  of scope.

The chroma planes are allocated whenever the frame buffers are, not only when
a chroma reader is registered, so a luma-only zero-copy run pays for them:
eight device planes (Cb and Cr, reference and distorted, two slots) and four
pinned host staging planes, each `ceil(w/2) x ceil(h/2)` samples. At 1080p
that is 4.1 MB of device memory and 2.1 MB pinned at 8 bit (8.3 MB and
4.1 MB at 10 bit); at 4K it is 16.6 MB and 8.3 MB at 8 bit (33 MB and
16.6 MB at 10 bit), half the size of the luma buffers. Against the
luma-only Stage-1 baseline, 1080p throughput on an Arc A380 was +1.9 %
(8 bit) and -3.9 % (10 bit), against a baseline that itself spread by 3 %
(Research-1595, "D-01 cost"); 4K was not measured. The DMA-BUF
Tile4 layout is the only one a real device has delivered so far (Arc A380);
the LINEAR and Y-tiled chroma layouts are covered by host-synthesised
vectors, not by hardware. Earlier builds returned `-EINVAL` (error number 22)
or crashed on a missing picture. Real QSV decode is tested by
`scripts/test/zerocopy-e2e.sh` (container, Intel GPU);
`test_sycl_zerocopy_guards` and `test_sycl_zerocopy_parity` cover every
extractor on shared device planes without a decoder.

## D3D11 staging-texture import (Windows)

`vmaf_sycl_import_d3d11_surface` accepts an `ID3D11Texture2D*` from a Windows
decoder (MediaFoundation, DXVA2, the Direct3D11 VideoProcessor). The
implementation:

1. creates a staging texture with `D3D11_USAGE_STAGING` and
   `D3D11_CPU_ACCESS_READ`,
2. calls `CopyResource` to pull the GPU surface into the staging texture,
3. maps the staging texture for CPU read, and
4. forwards the mapped pointer and row pitch to `vmaf_sycl_upload_plane`.

This is **not zero-copy**: throughput is bounded by PCIe upstream (the staging
map) and PCIe downstream (the SYCL host-to-device copy). A zero-copy
equivalent would need DXGI NT-handle sharing and DPC++ D3D11 interop, which
oneAPI does not document as of 2025.1. See
[`d3d11_import.cpp`](../../../core/src/sycl/d3d11_import.cpp) and ADR-0103.

## QSV / VA-API zero-copy: 10-bit correctness (ADR-1121)

The `libvmaf_sycl` FFmpeg filter runs VMAF directly on QSV-decoded VA-API
surfaces with no host round-trip. Two requirements must hold for it to produce
correct scores on a 10 or 12-bit pair. Get either wrong and you get
`VMAF score: nan` with a wildly inflated `integer_motion`.

### Give each decoder its own QSV session (required)

If both decoders are created against the **same** `-hwaccel_device`, FFmpeg
shares one `AVHWFramesContext` → one `mfxSession` → one VA surface pool between
them. The two decoders then write into the **same physical `VASurfaceID`s**, so
the reference decoder overwrites the surface the distorted decoder just produced
(decode-order look-ahead). The filter reads reference content where it expects
distorted content. Symptom: a checksum/score pattern where
`sycl_dis[N] == sycl_ref[N±1]`.

The fix is a usage contract — libvmaf cannot see FFmpeg's session topology from
inside the filter, so it is **not** auto-detected. Give **each** input its own
QSV device:

```bash
ffmpeg \
  -init_hw_device vaapi=va0:/dev/dri/renderD128 \
  -init_hw_device qsv=qsv_ref@va0 \
  -init_hw_device qsv=qsv_dis@va0 \
  -hwaccel qsv -hwaccel_output_format qsv -hwaccel_device qsv_dis -c:v av1_qsv -i dis.mkv \
  -hwaccel qsv -hwaccel_output_format qsv -hwaccel_device qsv_ref -c:v hevc_qsv -i ref.mkv \
  -lavfi '[0:v][1:v]libvmaf_sycl=log_fmt=csv:log_path=out.csv' \
  -frames:v 500 -an -sn -dn -f null -
```

`-an -sn -dn` keeps ffmpeg from decoding the audio, subtitle and data streams
of the inputs; see [start-up for short scenes](#start-up-for-short-scenes).

A single shared `qsv` device silently reintroduces the contamination.

Open the VA-API device on the render node directly, as above. Do not derive it
from a `drm` device (`-init_hw_device drm=drm0:/dev/dri/renderD128
-init_hw_device vaapi=va0@drm0`): in a rootless container only the render node
is usable, and that form fails before decoding with `Failed to set value
'drm=drm0:/dev/dri/renderD128' for option 'init_hw_device': Cannot allocate
memory`. If the host has more than one GPU, pick the render node of the Intel
one (`/sys/class/drm/renderD*/device/vendor` is `0x8086`). In a container, pass
the device with `--device /dev/dri`; the `vmafx-zerocopy-fix` image from
`Containerfile.vmafx` runs this command as is.

### Feature routing, supported surfaces and import failures (ADR-1595)

On QSV zero-copy input the `libvmaf_sycl` filter resolves each `feature=` name
to its SYCL twin and refuses to configure when none can run (a CPU extractor
cannot read device-only frames); on software input it uses the twin when there
is one and otherwise warns and computes the feature on the CPU. Zero-copy
accepts NV12 and P010 surfaces only, and a failed VA import aborts the run
instead of skipping a frame. The messages and examples are in
[Using VMAF with FFmpeg](../../usage/ffmpeg.md#how-feature-names-are-resolved-in-libvmaf_sycl);
the rationale is
[ADR-1595](../../adr/1595-sycl-zerocopy-fail-loud-twin-routing.md).

### P010/P012 pixels are normalized in the import

VA-API stores 10-bit (P010) / 12-bit (P012) samples **MSB-aligned**
(`V_MSB = V_LSB << (16 − bpc)`), while the VMAF feature kernels expect
LSB-aligned `bpc`-bit integers. The CPU `libvmaf` path never sees this because
FFmpeg auto-converts `P010LE → YUV420P10LE` (a `>> 6` shift) to satisfy the
filter's pixel-format list.

The SYCL import does the equivalent luma-only `>> (16 − bpc)` shift — **fused
into the Tile4 / Y-tiled de-tile kernel** on the QSV hot path (no extra GPU
pass), and a standalone `launch_p010_normalize()` kernel on the rare LINEAR /
readback fallbacks. It is a no-op for 8-bit NV12.

This is internal — no user action required — but explains why a hand-rolled VA
import that skips it sees `integer_motion` inflated by exactly `2^(16 − bpc)`
(64× at 10-bit).

## Throughput on Arc A-series (ADR-1769)

What to expect from `libvmaf_sycl` on QSV zero-copy input, what limits it and
how to measure it yourself. The numbers come from an Intel Arc A380 scoring a
3840x1600 10-bit HDR10 pair (HEVC reference, AV1 distorted, both decoded by
QSV), video only, with the library at the end of phase 13. The research
digest has every run:
[Research-1769](../../research/1769-sycl-zerocopy-throughput-a380.md).

### What to expect

Steady state is the frame rate after start-up, from a 600-frame and a 20-frame
run: `(600 - 20) / (rtime600 - rtime20)`. GPU time is the filter's own time per
frame. Each value is the median of three interleaved runs.

| Model | Steady fps before | Steady fps now | GPU ms per frame before / now |
| --- | --- | --- | --- |
| `vmaf_v0.6.1` | 43.9 | 45.9 (+4.5 %) | 22.60 / 21.58 |
| `vmaf_4k_v0.6.1` | 43.6 | 46.2 (+5.9 %) | 22.66 / 21.44 |
| `vmaf_v1.0.16_3d0h` | 50.9 | 41.8 (-17.8 %) | 18.77 / 22.97 |

A whole 53-minute episode (76378 frames) with `vmaf_4k_v0.6.1` ran at 47.44 fps
against 44.99 fps before (+5.4 %). Every frame's scores are identical between
the two builds, and the pooled score is 95.454927 on both.

`vmaf_v1.0.16_3d0h` is slower than before because its SpEED features are now
exact: the SYCL SpEED covariance used to round differently from the CPU, so
`speed_chroma` and `speed_temporal` could differ by one fp32 step on rare
frames (frame 140 of the test segment). Every SYCL build before the fix had
this. The SYCL twin now matches the CPU bit for bit. The exact covariance
alone cost 9.70 ms per frame (18.83 to 28.53 ms of GPU time). Splitting its
work across three launches
([ADR-1931](../../adr/1931-sycl-speed-covariance-fast-exact.md)) brought that
down to 23.06 ms, which recovers 56 to 59 % of the cost, depending on the
measurement.

The frame time is GPU-bound, so a faster GPU or `n_subsample` is what raises
throughput; see
[speed against accuracy](../../usage/ffmpeg.md#speed-against-accuracy-with-libvmaf_sycl).

### What limits the frame

- **The compute engine is busy 96 % of the frame.** At the start of phase 13
  the GPU tasks of a `vmaf_v0.6.1` frame took 19.9 ms: VIF 11.0 ms (its
  scale-0 horizontal pass alone 6.33 ms), ADM 6.3 ms, motion 1.25 ms and the VA
  import 1.1 ms. About 37 launches per frame left 2.2 ms of gaps.
- **The Blitter is not on the critical path.** Its busy time follows the
  feature kernels and overlaps the compute engine. It is not the decoder (0 in
  a decode-only run) and not the import (0.86 ms).
- **The host waits for the GPU.** In the baseline, the host spent 19.4 ms of a
  21.8 ms frame in the frame-start wait, about one CPU core spinning. Moving
  that wait into the VA import was measured and changed neither the frame rate
  nor the host CPU time, because libvmaf still waits for every frame before it
  collects scores. The host wait has no setting, and a lower-power wait was
  not built.

### What changed

- **Kept:** the scale-0 VIF horizontal pass reads its inputs from a
  local-memory tile (K1). That kernel went from 6.32 to 5.33 ms per frame, and
  the filter's GPU time dropped by 1.16 ms per frame. Scores are unchanged.
- **Fixed:** `n_subsample` above 1 returned wrong `integer_motion2` /
  `integer_motion3` (and `vmaf` up to 100) on the SYCL path; it now matches
  the CPU.
- **Fixed:** the SpEED covariance, as described above.
- Measured and reverted because they gained nothing: the device-side fence on
  the VA import (C3), a 16-byte Tile4 de-tile (K3), and the same tiling on VIF
  scales 1 to 3. [ADR-1769](../../adr/1769-sycl-zerocopy-throughput-a380.md)
  lists every candidate with its measurement.

### Why not faster

Phase 13 aimed for 15 % more frames per second on `vmaf_v0.6.1` and reached
4.5 %. A 15 % gain needs 19.8 ms per frame; the frame is now 21.8 ms. The
compute engine is busy 96 % of that time, so overlapping the import with
compute has no idle time left to use. Most of the frame is VIF and ADM
arithmetic. Two candidates were not tried: faster ADM arithmetic and merging
small launches. The digest's
[roofline](../../research/1769-sycl-zerocopy-throughput-a380.md#roofline-and-measured-ceilings)
puts their ceilings at about 7 % and 4 % of the frame.

### Start-up for short scenes

The `libvmaf_sycl` initialisation (device, queue, model, buffers) takes about
0.25 s. With an image built for the default ahead-of-time targets (the
`vmafx-zerocopy-fix` image from `Containerfile.vmafx`), a 1-frame run takes
0.47 s and a 20-frame run 0.85 s. After start-up each frame
costs about 20 ms.

- **Score video only.** Without `-an -sn -dn` (or explicit `-map` options for
  the video streams) ffmpeg picks an audio track and decodes all of it, even
  when `trim` cuts the video to a few frames. On a 53-minute episode that was
  11.5 s per ffmpeg process.
- **A JIT build compiles its kernels at start-up** (7.1 s here). Set
  `SYCL_CACHE_PERSISTENT=1` and `NEO_CACHE_PERSISTENT=1` with a persistent
  cache directory to skip that on later runs. An image with ahead-of-time
  device code does not need it.
- **Reuse one container for many scenes.** `podman run` adds about 0.4 s per
  scene. Run the scenes in one long-lived container (`podman exec`, or a loop
  inside one `podman run`).
- **Parallel scenes.** Two or three scenes at once can hide one scene's
  start-up behind another's GPU work. They cannot go past the GPU's own limit
  of about 46 fps for `vmaf_v0.6.1`. Parallel runs were not measured.

### Measure it yourself

`scripts/test/zerocopy-throughput.sh` runs a segment of a real pair in the
SYCL toolchain container, cut with `trim=end_frame=N` on both inputs, and
records fps, the filter's GPU time, host CPU time and the GPU engine counters:

```bash
SYCL_DEV_MEDIA_DIR=<media dir> scripts/test/sycl-dev-container.sh exec \
  bash scripts/test/zerocopy-throughput.sh --ref <ref> --dis <dis> \
  --frames 200 --model vmaf_v0.6.1 --out /work/.cache/sycl-dev/bench
```

A 200-frame run is dominated by start-up. For a frame rate, compare a
600-frame and a 20-frame run with the steady-state formula above. With
`VMAF_SYCL_TIMING=1` the filter also prints a `[vmaf-sycl] phases:` line with
the host time spent in waits and in the VA import
([overview](overview.md)). The per-extractor waits of that mode slow the run
down, so do not quote frame rates from an instrumented run.

## Picture pre-allocation

`vmaf_sycl_preallocate_pictures()` + `vmaf_sycl_picture_fetch()` back a
2-deep ring of USM-backed `VmafPicture` instances that callers hand to
`vmaf_read_pictures()`. Three modes:

| `pic_prealloc_method` | Backing | Use case |
| --- | --- | --- |
| `VMAF_SYCL_PICTURE_PREALLOCATION_METHOD_NONE` | No pool; `vmaf_sycl_picture_fetch` falls back to host `vmaf_picture_alloc` | CPU-fed pipelines, test harnesses |
| `VMAF_SYCL_PICTURE_PREALLOCATION_METHOD_DEVICE` | `sycl::malloc_device` (GPU-resident) | Zero-copy decoder interop (decoder writes directly into device USM) |
| `VMAF_SYCL_PICTURE_PREALLOCATION_METHOD_HOST` | `sycl::malloc_host` (coherent, CPU-visible) | Decoders that must write from the CPU but want pool reuse |

The pool depth (2) matches the double-buffered shared-frame upload in
`VmafSyclState`, so frame N+1 can start filling slot 1 while frame N's
compute still consumes slot 0. The caller owns the ref returned by
`vmaf_sycl_picture_fetch` and must release it via `vmaf_picture_unref` when
done with it; the pool retains its own ref until `vmaf_close()` returns exactly
zero. A nonzero close status retains the pool with the teardown-only context.

Minimal example:

```c
VmafSyclPictureConfiguration cfg = {
    .pic_params = { .w = 1920, .h = 1080, .bpc = 8, .pix_fmt = VMAF_PIX_FMT_YUV420P },
    .pic_prealloc_method = VMAF_SYCL_PICTURE_PREALLOCATION_METHOD_DEVICE,
};
vmaf_sycl_preallocate_pictures(vmaf, cfg);

for (unsigned i = 0; i < n_frames; i++) {
    VmafPicture ref, dis;
    vmaf_sycl_picture_fetch(vmaf, &ref);   /* device USM, caller writes */
    vmaf_sycl_picture_fetch(vmaf, &dis);
    /* ... fill ref.data[0] and dis.data[0] via decoder/upload ... */
    vmaf_read_pictures(vmaf, &ref, &dis, i);
}
vmaf_read_pictures(vmaf, NULL, NULL, 0);
```

See [ADR-0101](../../adr/0101-sycl-usm-picture-pool.md) for the design
rationale (Y-plane only, pool depth 2, refcount semantics).
