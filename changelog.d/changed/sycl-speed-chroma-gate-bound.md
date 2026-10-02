- **The parity gate bounds `speed_chroma` between the CPU and the SYCL twin at
  `5e-6` instead of `5e-5`**, the bound the CUDA and HIP twins of the same
  device chain already have (ADR-1430, ADR-1452). On an Arc A380
  `speed_chroma_sycl` equals the CPU extractor of its own icx build and
  `speed_chroma_cuda` on 918 of 918 values; against a GCC build's CPU 15
  differ, by 1.9e-6 at most, because glibc's `log2f` is not correctly rounded
  and Intel's is. The cell is therefore a bound, not an exact declaration. No
  score changes.
