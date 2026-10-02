---
paths:
  - ai/src/vmaf_train/data/frame_loader.py
invariant: Frame loader ingests gray and packed rgb24/bgr24/rgba/bgra; planar or subsampled formats rejected.
---
<!-- markdownlint-disable MD013 MD060 -->
# Frame loader pixel formats

`ai/src/vmaf_train/data/frame_loader.py` = direct ffmpeg frame
ingest seam for C2/C3 training. Accepts `gray` as `HxW` arrays and
packed `rgb24` / `bgr24` / `rgba` / `bgra` as `HxWxC` arrays. Do not
silently accept planar or subsampled formats such as `yuv420p` in this
loader; those need explicit plane semantics before they're safe to
feed into training tensors.
