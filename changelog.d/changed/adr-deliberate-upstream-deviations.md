- **Eight deliberate differences from Netflix's libvmaf are recorded, each with
  its measured size and the upstream pull request that would end it**
  (ADR-1479 to ADR-1486): `ciede` on 4:2:2, `speed_temporal` with
  `speed_prescale` above 1, a failing extractor failing the run, integer `adm`
  on frames of 17 to 32 pixels, chroma planes of odd-sized pictures,
  `float_ms_ssim` on anti-correlated frames, `apsnr` of a plane without error,
  and `float_motion` with `motion_add_scale1` and `motion_add_uv`. No
  behaviour changes; the scores were already these.
