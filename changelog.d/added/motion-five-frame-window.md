- **`motion_five_frame_window` works, and the four `vmaf_v1.0.16_hfr_*` models
  score.** The option of the `motion` and `motion_v2` extractors takes each
  frame's SAD against the frame two back, and `motion2` from the SADs of the
  frames before and after; the fork declared it and returned `-ENOTSUP`
  (ADR-0337, ADR-0994), so the HFR models that set it could not be used. It
  is Netflix's code (`a2b59b77`, `a4a1492d`) with its arithmetic unchanged:
  on 31 clips, with eight option sets, on the scalar, AVX2 and default paths,
  serial and with worker threads, all 95 130 values equal Netflix `9e48141b`
  at 17 significant digits. `--model version=vmaf_v1.0.16_hfr_3d0h` and its
  three siblings run on every backend; on `cuda`, `sycl`, `hip` and `metal`
  the motion feature is computed by the CPU extractor and the rest on the
  device. See [Motion, five-frame window](docs/metrics/motion.md#five-frame-window),
  [VMAF v1 models](docs/models/v1.md) and
  [ADR-1478](docs/adr/1478-motion-five-frame-window-port.md).
- **A preallocated picture pool needs four pictures for the five-frame
  window, and only for it.** While an extractor with the option is registered
  the library keeps the reference pictures of the two frames before the
  current one; otherwise it keeps what it kept before. A pool from
  `vmaf_preallocate_pictures()` below four pictures next to such an extractor
  is refused with `-EINVAL` and one error line naming `pic_cnt` and the
  minimum, whichever of the two calls comes second, instead of stalling on the
  third frame. Without the option a pool of three works as before. The `vmaf`
  tool preallocates four pictures in a run without `--threads` (three
  before). Callers that allocate each picture with `vmaf_picture_alloc()`,
  the FFmpeg filters among them, need no change. See
  [the C API reference](docs/api/index.md#ownership-and-lifetime).
