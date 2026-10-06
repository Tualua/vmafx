- **zimg picture conversion allows approximate gamma (port of Netflix/vmaf `5c3f4fb90`).**
  `vmaf_picture_convert()` builds its zimg graph with `allow_approximate_gamma`, as FFmpeg's
  `zscale` does: exact transfer functions are about 20 times slower for PQ and change scores
  negligibly. Only builds with `-Denable_zimg=true` are affected. See
  [Pictures](docs/api/pictures.md#converting-pictures-vmaf_picture_convert).
