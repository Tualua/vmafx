# Intel Arc + VAAPI driver priority

## TL;DR

If `vainfo` reports `Driver version: VA-API NVDEC driver` on a system that has
both NVIDIA and Intel Arc cards, NVIDIA's NVDEC-VAAPI shim has shadowed the
real iHD driver for Arc. Force the Intel driver explicitly when you want to use
Arc QSV or VAAPI:

```sh
LIBVA_DRIVER_NAME=iHD ffmpeg -init_hw_device qsv:hw,child_device=/dev/dri/renderD129 ...
```

The render node name (`renderD129` here) differs per host. List yours with
`ls -l /dev/dri/by-path/`, as
[docker-production.md](docker-production.md) shows for GPU containers.

## Why this happens

NVIDIA ships a VA-API shim (`libva-driver-nvidia`) so Chrome and Firefox can
hardware-accelerate decode through their VAAPI code path without caring that
the underlying driver is CUDA-NVDEC. On a multi-card host the shim is often
picked up first by libva's auto-detection, and any Arc-targeted call
(`h264_qsv` or `h264_vaapi` against `/dev/dri/renderD129`) then routes through
NVIDIA's translation layer instead of Intel's iHD.

The symptom is:

```text
Error creating a MFX session: -9. Device creation failed: -1313558101.
```

MFX is Intel's Media SDK / oneVPL session protocol. NVIDIA's shim does not
speak it, so the handshake fails.

## Fix

Set `LIBVA_DRIVER_NAME=iHD` whenever you invoke ffmpeg with QSV or VAAPI
encoders against the Arc card. This is a manual step: no script in the
repository sets it for you, including
[`scripts/dev/hw_encoder_corpus.py`](../../scripts/dev/hw_encoder_corpus.py),
so export it in the shell that runs the QSV encode.

## Verification

```sh
LIBVA_DRIVER_NAME=iHD vainfo --display drm --device /dev/dri/renderD129
# Expect: Driver version: Intel iHD driver for Intel(R) Gen Graphics - 26.x.x
```

The version number in the expected line is illustrative.
