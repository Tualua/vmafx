- **Integer ADM scores pictures whose contrast-masking row passes INT64_MAX.**
  The scale-0 masking reduction summed each row of non-negative cubes in a
  signed 64-bit integer. A 64-pixel-wide picture with full-range detail and no
  distortion took one row past INT64_MAX at the default settings, and any
  width did with a CSF weight near its limit: the sum wrapped and the frame
  failed with `invalid ADM reduction`. The CPU extractor, its AVX2 / AVX-512
  paths and the CUDA, HIP, SYCL and Metal twins sum the scale-0 rows unsigned
  now; scores that did not overflow are unchanged.
