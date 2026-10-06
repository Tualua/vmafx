- **The ADM twins' decouple header is tested more, the Metal scale-0 angle flag sums in 64 bits, and a CUDA/HIP corner is recorded (`T-GPU-ADM-ANGLE-FLAG-S0-INT32-CORNER-2026-10-06`).**
  `test_adm_decouple_recip_{cuda,hip}` (the twins' own header compiled for the host) now also holds `decouple_r_s123()`, `get_best15_from32()`
  and both angle flags to the CPU's over 800 000 random draws and the corners of the int16 range, and each executable has its own
  `run_tests` root. That clears CodeQL `cpp/unused-static-function` alerts 1464-1479 (the header's functions are all used by a host
  build now) and exposes a real corner: with every band at -32768 the CUDA and HIP angle flag adds in int32 and wraps where the CPU's
  int64 sum does not (17 of 256 corner combinations; the fix costs `adm_cm_aim_line_kernel_4` registers past its budget, so the row is
  open). Metal's `iadm_angle_flag_s0()` had the same sums and is fixed. The two `cpp/include-non-header` findings of the
  device-source tests (1463, 1488) are declared exceptions (`codeql-include-non-header.toml`).
