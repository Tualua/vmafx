- **`float_adm` uses AVX2 and AVX-512 on x86, with unchanged scores
  (ADR-1473).** The wavelet and the contrast-sensitivity stage run through
  kernels that return the scalar code's bits: every four-tap sum starts at
  `+0` and multiplies before it adds, and the filtered CSF value is a double
  product narrowed to float. `--cpumask` selects scalar (63), AVX2 (48) or
  AVX-512 (0); every `float_adm` output and the float model scores are
  identical on all three and identical to the previous release. One thread on
  a Ryzen 9 9950X3D: 1.45 to 1.11 ms per 576x324 frame, 17.9 to 15.1 ms at
  1920x1080, 76.2 to 62.9 ms at 3840x2160. The kernels existed but nothing
  called them, and they differed from the scalar code (a negative zero where
  it returns a positive one, a float product where it multiplies in double).
  Two unused reduction kernels that could not reproduce the scalar sums
  (`float_adm_csf_den_scale_avx2` / `_avx512`, `float_adm_sum_cube_avx2` /
  `_avx512`) are removed.
