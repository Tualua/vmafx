- The HIP device sources of CAMBI, CIEDE, MS-SSIM and SpEED, the HIP CAMBI replay
  test, the float ADM math probe and the SYCL fp-arith contract test now pass the
  hip-lane clang-tidy profile with zero findings (ADR-1142). Scores are unchanged:
  the HIP parity tests of the touched twins compare them with the CPU extractor
  on gfx1036.
