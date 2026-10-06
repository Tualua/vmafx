- The HIP kernels of float and integer ADM, float moment, float and integer
  motion, float and integer PSNR, motion v2 and float SSIM now pass the hip-lane
  clang-tidy profile with zero findings (ADR-1142). Scores are unchanged: the
  HIP parity tests of the touched twins compare them with the CPU extractor on
  gfx1036.
