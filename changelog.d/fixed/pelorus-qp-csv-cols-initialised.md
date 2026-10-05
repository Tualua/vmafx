- **The vendored Pelorus x265 CSV reader builds without warnings under an
  optimising GCC.** `x265_csv_read_rows()` in
  `core/src/interop/pelorus_qp_report_csv.c` left its column-index struct
  uninitialised until the header row was seen, and GCC 16 at `-O2 -Wall -Wextra`
  raised seven `-Wmaybe-uninitialized` warnings (`type`, `poc`, `qp`, `bits`,
  `psnr_y`, `psnr_u`, `psnr_v`). Every index now starts at -1 (absent), which the
  reader already treats as a missing column. The fix is in VMAFx/pelorus
  (#79) and re-vendored here (pin `42cb17106a2d`); the parsed values are
  unchanged.
