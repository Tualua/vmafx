- **`test_adm_decouple_recip_cuda` / `_hip` build with MSVC and finish on
  MinGW.** The host build of the twin's header named `__builtin_clz`, which
  cl.exe does not have, without the `compat_builtin.h` shim (C3861 on every
  Windows MSVC leg), and its CPU helpers refilled the 65 537-entry
  `div_lookup` table for every sample on Windows, past the 120 s timeout on
  the UCRT64 leg. `check-msvc-clz-shim.sh` now requires the shim in every host
  file that names `__builtin_clz`.
