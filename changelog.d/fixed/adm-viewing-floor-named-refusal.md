- **Integer ADM reports named refusal for invalid viewing geometries.** When
  `adm_norm_view_dist * adm_ref_display_height < 3240`, integer ADM (`adm` on CPU
  and GPU twins `adm_cuda`, `adm_hip`, `adm_sycl`, `adm_metal`) now logs an
  explanatory message naming the extractor, the option values, their product, the
  3240 floor (`1080p at 3H`), and `float_adm` as the accepting alternative,
  instead of failing with an unexplained `-EINVAL`.
