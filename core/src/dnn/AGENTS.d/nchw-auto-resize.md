---
paths:
  - core/src/dnn/tensor_io.c
  - core/src/dnn/tensor_io.h
  - core/test/dnn/test_tensor_io.c
invariant: NCHW auto-resize defaults to disabled with half-pixel-center coordinates and pass-through on matched dimensions.
---
<!-- markdownlint-disable MD013 -->
# NCHW Input Auto-Resize and Coordinate Conventions

## Invariant — NCHW auto-resize default is DISABLED (ADR-0550)

`vmaf_ctx_dnn_run_frame_nchw` supports auto-resampling luma plane
to model's expected NCHW input shape when they differ, using
filter selected by `vmaf->dnn.resize_mode` (0=DISABLED, 1=BILINEAR,
2=NEAREST, 3=BICUBIC). Enum integer layout is shared between
public `VmafDnnResizeMode` (`core/include/libvmaf/dnn.h`) and
internal `VmafTinyResize` (`core/src/dnn/tensor_io.h`); values
**must** stay 0-indexed and aligned across two enums — public
setter casts directly without remapping.

- **Default zero-init is DISABLED**: `vmaf_init` does
  `memset(v, 0, sizeof(*v))`, and `VMAF_TINY_RESIZE_DISABLED == 0`,
  so any context never calling `vmaf_dnn_set_resize_mode` gets
  strict -ERANGE-on-mismatch behaviour. Renumbering enums to
  put different value at 0 would silently change default for
  every existing caller. Operator must pass `--tiny-resize bilinear`
  (or equivalent) to enable auto-resize.
- **Matched-dims path stays bit-identical to `vmaf_tensor_from_luma`**:
  `vmaf_tensor_from_luma_resize` forwards verbatim to
  `vmaf_tensor_from_luma` when `src_w == dst_w && src_h == dst_h`.
  This keeps Netflix golden gate unaffected (FR tiny models
  never hit resize branch — user-supplied ref/dist pair is
  already at right dims). Never introduce per-pixel codepath
  for matched-dims case.
- **`DISABLED` semantics live in `libvmaf.c`, not in helper**:
  per-frame dispatch routes `VMAF_TINY_RESIZE_DISABLED` to
  `-ERANGE` before calling resize helper. Helper itself
  returns `-EINVAL` when handed `DISABLED` so programming bug
  surfaces loudly. Keep gates in both places — pulling either
  gate folds disabled-mode semantics into single point that's
  easier to regress.
- **Coordinate convention is half-pixel-centre**:
  `sx = (dx + 0.5) * src_w / dst_w - 0.5` (and analog for `sy`).
  This matches OpenCV `INTER_*` and torchvision
  `Resize(..., antialias=False)`. Out-of-bounds source coords clamp
  via replicate-edge. Changing convention silently re-trains
  every shipped image-input model against slightly different
  distribution.

`test_resize_*` regressions in
[`../../test/dnn/test_tensor_io.c`](../../../test/dnn/test_tensor_io.c)
gate bit-identical-identity / disabled-EINVAL / nearest
floor-coord behaviour.
