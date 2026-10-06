- **HDR-VMAF groundwork from upstream: input colorimetry, a model `conversion_target`,
  conversion in `vmaf_read_pictures()` (ports of Netflix/vmaf `ed61076b2`, `1ddf81607`,
  `a6c0ba6d5`, `130569c45`, `efe90c8b8`, `5c3f4fb90`; [ADR-2093](docs/adr/2093-upstream-hdr-groundwork-input-colorimetry.md)).**
  A model file may declare a `conversion_target` (colorspace, optional pixel format and
  bit depth); `vmaf_read_pictures()` then converts both pictures to it with zimg before
  extraction, on the host and before any GPU upload. The source colorimetry comes from the
  new `--color_range_ref/_dist`, `--color_primaries_ref/_dist`, `--color_trc_ref/_dist` and
  `--color_matrix_ref/_dist` flags (all four of an input, or none) and, in the C API, from
  the new `vmaf_set_input_colorimetry()`: `VmafPicture` keeps its layout, so the colour is
  declared on the context instead of in each picture. Models without a target, which is
  every shipped model, are unaffected. The Python harness passes `color_ref` / `color_dist`
  from `optional_dict` to `vmafexec`. zimg stays the opt-in `-Denable_zimg=true`. See
  [CLI](docs/usage/cli.md#input-colorimetry),
  [model files](docs/models/v1.md#model-declared-conversion-target) and
  [Pictures](docs/api/pictures.md#converting-to-a-models-conversion-target).
