---
paths:
  - core/src/feature/adm_csf_tools.h
  - core/src/feature/adm_tools.c
  - core/src/feature/adm_tools.h
  - core/src/feature/barten_csf_tools.h
  - core/src/feature/ciede.c
  - core/src/feature/integer_adm.h
  - core/src/feature/integer_ssim.c
  - core/src/feature/speed.c
  - core/src/feature/speed_internal.c
  - core/src/feature/speed_qa.c
  - core/src/feature/vif_tools.c
  - core/src/feature/y_funque_plus.c
  - core/src/feature/sycl/float_adm_sycl.cpp
  - core/src/feature/sycl/integer_adm_sycl.cpp
invariant: No TU defines M_PI / M_E; <math.h> provides them, Windows via meson's -D_USE_MATH_DEFINES.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Math constants come from `<math.h>` (Netflix/vmaf 4e150067b)

- No translation unit defines `M_PI`, `M_E` or `_USE_MATH_DEFINES`. Linux gets
  the constants from `_GNU_SOURCE`, macOS from `_DARWIN_C_SOURCE`, Windows
  (MSVC, clang-cl, icx-cl, MinGW-w64) from `-D_USE_MATH_DEFINES`, all set as
  project arguments in `core/meson.build`; nvcc gets `-D_USE_MATH_DEFINES` in
  `cuda_flags`. On the command line the define precedes every `<math.h>`
  include, which a per-file `#define` could not guarantee.
- The removed literals (`3.14159265358979323846`, `...264338327`,
  `3.141592653589793238462643`) are the same double as glibc's and MinGW's
  `M_PI` (`0x1.921fb54442d18p+1`); x86 object code was identical apart from
  moved line numbers.
- **On upstream sync**: a file that brings back `#ifndef M_PI` /
  `#define _USE_MATH_DEFINES` takes the fork's side (delete it).
