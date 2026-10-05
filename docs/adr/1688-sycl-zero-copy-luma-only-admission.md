<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1688: The SYCL zero-copy path admits only extractors that compute from the shared luma, and names every other one

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: Lusoris
- **Tags**: sycl, ffmpeg, api, correctness, fork-local

## Context

`vmaf_read_pictures_sycl()` is the zero-copy read path. FFmpeg's
`libvmaf_sycl` filter (patch `0005`) uses it on QSV frames, and a program of
its own can use it with `vmaf_sycl_import_va_surface()`,
`vmaf_sycl_dmabuf_import()` or `vmaf_sycl_upload_plane()`. The import puts
only the luma plane into the state's shared frame
([ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md)). The read calls
every SYCL extractor's `submit()` with no picture.

The default model is `vmaf_v1.0.16_3d0h` (`core/include/libvmaf/model.h`).
It needs `speed_chroma_uv`, and `speed_chroma_sycl` reads U and V from the
host pictures. On an Arc A380 with QSV input, before this decision:

- the default model failed on the first frame with `vmaf_read_pictures_sycl
  failed: -22`, and nothing in the output mentioned chroma;
- `float_psnr_sycl` dereferenced the missing picture and FFmpeg died with a
  segmentation fault. The read showed the same NULL dereference in
  `float_adm_sycl`, `float_motion_sycl`, `float_vif_sycl`,
  `integer_ssim_sycl` and `float_ms_ssim_sycl`;
- `motion_sycl` with `motion_add_uv=true` added the SAD of chroma staging the
  path never fills. `integer_motion2_mau` equalled `integer_motion2`
  (4.257894 on frame 1) where the host path gives 5.536504;
- a CPU extractor was skipped on every frame. Asked for `float_psnr`, the
  run reported nothing for it and no error.

The documentation said the zero-copy path was luma-only.
`docs/backends/sycl/history.md` also said the default model was luma-only,
which stopped being true when the default moved to v1.0.16.

## Decision

`VmafFeatureExtractor` gains an optional hook, `reads_shared_luma_only()`. It
answers, for the options in `priv`, whether the extractor computes from the
shared device luma alone. `vmaf_feature_extractor_reads_shared_luma_only()`
answers false for a CPU extractor and for a SYCL extractor without the hook.

Before `vmaf_read_pictures_sycl()` counts a frame or advances the
double-buffer slots, it checks every registered extractor. It logs one error
per extractor that cannot run and names it, then returns `-ENOTSUP`.

These extractors answer true:

- `adm_sycl`, `vif_sycl`, `motion_v2_sycl`, `cambi_sycl` and
  `float_moment_sycl`, always;
- `motion_sycl`, unless `motion_add_uv` is set;
- `psnr_sycl` and `psnr_hvs_sycl`, with `enable_chroma=false`.

`vmaf_flush_sycl()` skips an extractor that never saw a frame, so a refused
context flushes cleanly. The FFmpeg filter turns `-ENOTSUP` into a message
that points at the host-frame bridge. It counts only frames libvmaf
accepted, and prints no score line after a failed pooled score.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Admission check with a per-extractor, option-aware hook (chosen) | Every case fails before any work, with the extractor's name, and the extractor knows its own options. `vmaf_v0.6.1` keeps running zero-copy and keeps the CPU's scores. The same pattern as `reads_prev_prev_ref()` (ADR-1478). | Eight SYCL files gain a hook. A new twin that reads host pictures must leave the hook out, which a device-free test checks. | Chosen. |
| Import the chroma too: de-interleave the NV12 / P010 UV layer in the de-tile kernel, and give every chroma twin a device chroma path | The default model would run zero-copy. | A new de-tile kernel per tiling mode, plus device chroma paths in `speed_chroma`, `ciede`, `ssimulacra2`, `psnr`, `psnr_hvs` and `motion` add-uv. Each must stay bit-exact and scratch-free on 19 AOT targets. That is a feature, not a fix. | Deferred to the post-1.0 zero-copy import of [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md); it needs its own ADR. The admission check still applies to host-luma twins. |
| A per-extractor flag bit instead of a hook | Simpler. | `motion_add_uv` and `enable_chroma` change the answer at run time, and a flag cannot express that. | Rejected. |
| Fail inside each twin's `submit()` on a NULL picture, with a message | Local. | Fifteen files to change. The first failing twin hides the others. A CPU extractor is still skipped, because it never gets a `submit()` on this path. | Rejected. The single check covers every case. |
| Fall back to the CPU for an extractor that cannot run zero-copy | The model would score. | The path has no host picture to give the CPU, so there is nothing to fall back to. A silent fallback is against the fork's rules anyway. | Not possible. |

## Consequences

- **Positive**: no zero-copy run crashes, reads stale chroma, or drops a
  feature without an error. Each refusal names the extractor. `vmaf_v0.6.1`
  is unchanged and equals the CPU frame for frame (48 of 48 frames on the
  A380 through FFmpeg, 3 of 3 at `%.17g` in
  `test_sycl_zero_copy_model_gate`).
- **Negative**: the default model, and any model or feature that needs
  chroma, cannot use the zero-copy path. They need the host-frame bridge.
  Before this decision they could not use it either; they failed, crashed, or
  scored wrong.
- **Neutral / follow-ups**: chroma import is the follow-up that would let the
  default model run zero-copy; it belongs to the post-1.0 embedding milestone
  ([ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md)). `test_sycl_zero_copy_admission` (device-free)
  pins every SYCL extractor's answer. `test_sycl_zero_copy_model_gate` (on a
  device) pins the refusals and the `vmaf_v0.6.1` scores.

## References

- [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md) (the luma import and
  its P010 shift), [ADR-1369](1369-sycl-shared-planes-light-twins.md) (the
  shared planes), [ADR-1478](1478-motion-five-frame-window-port.md)
  (the `reads_prev_prev_ref()` hook this one copies).
- A380 runs, 2026-10-05: container `vmaf-dev-mcp` (FFmpeg n9.0.2 with the
  series, libvmaf 1.0.0-rc.2), QSV decode of the Netflix 576x324 pair encoded
  as H.264 High (`-crf 8`), one QSV session per decoder.
- Source: maintainer task of 2026-10-05, paraphrased: find out what the SYCL
  zero-copy path does with the default model, then either import the chroma
  planes or fail with an error naming the missing chroma when the model needs
  it, and correct `docs/backends/sycl/history.md`.
