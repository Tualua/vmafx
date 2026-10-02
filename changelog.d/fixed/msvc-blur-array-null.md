- **The Windows MSVC builds compile `blur_array.c` again.** A lint cleanup on
  2026-10-02 had replaced `NULL` with the C23 keyword `nullptr` in that C file;
  MSVC's C mode does not know the keyword.
