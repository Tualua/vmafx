- **The Windows MSVC builds compile the SSIM, MS-SSIM, float VIF and motion
  sources again.** Lint cleanups on 2026-10-02 had replaced `NULL` with the C23
  keyword `nullptr` in seven C files; MSVC's C mode does not know the keyword.
