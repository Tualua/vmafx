- **`vmaf_picture_convert()`: zimg picture conversion, additive variant
  (port of Netflix/vmaf `0497a0f29`, [ADR-1822](docs/adr/1822-additive-picture-convert.md)).**
  New public API in `libvmaf/picture.h`: `VmafColor`, the colour enums,
  `VmafPictureConvertTarget`, `vmaf_picture_convert_context_init_with_color()`,
  `vmaf_picture_convert()` and `vmaf_picture_convert_context_close()`. It converts
  pixel format, bit depth, size and colour description through zimg (>= 2.7),
  enabled with the new `-Denable_zimg=true` Meson option (default off; the
  functions return `-ENOTSUP` without it). `VmafPicture` is unchanged: the
  source colour is passed as an argument instead of the `VmafPicture::color`
  member upstream adds, which would move `ref` and `priv`. See
  [Pictures](docs/api/pictures.md#converting-pictures-vmaf_picture_convert).
