- **The MSVC build's AVX2 and AVX-512 float ADM wavelet returns the scalar
  code's bits on signed zeros** (`T-MSVC-FLOAT-ADM-X86-TEST-FAILS-2026-10-04`).
  MSVC removes an intrinsic addition of +0 even under `/fp:precise`, so a
  sample whose four products were all -0 came out -0 on the vector path and
  +0 in `adm_dwt2_s()`, and `test_float_adm_x86` failed on the Windows tester
  zip. The kernels now form that first `+0 +` with a compare and a mask, which
  MSVC keeps. Picture data never reached the case, so no score changes. The
  `Windows MSVC+CUDA (full)` CI lane now runs `test_float_adm_x86`.
