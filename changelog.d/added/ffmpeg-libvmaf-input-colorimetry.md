- **The FFmpeg `libvmaf` filter declares the input colour to libvmaf (FFmpeg patch 0022, [ADR-2093](docs/adr/2093-upstream-hdr-groundwork-input-colorimetry.md)).**
  On the first frame pair the filter (and the software path of `libvmaf_sycl`) maps the AVFrame
  range, primaries, transfer and matrix of each input and calls `vmaf_set_input_colorimetry()`,
  so a model with a `conversion_target` converts HDR input. Inputs without colour tags, and
  models without a target, score as before. See
  [FFmpeg usage](docs/usage/ffmpeg.md#input-colour-tags-and-hdr-models).
