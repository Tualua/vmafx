- **The CUDA, HIP and SYCL `speed_chroma` / `speed_temporal` twins divide each
  covariance sum by the exact element count.** They divided by the count
  rounded to fp32, which differs from the count above 2^24 elements per
  submatrix (a picture wider than 16K with `speed_prescale` above 2) and moved
  the covariance away from the CPU extractor's. Scores of every picture up to
  16K are unchanged.
