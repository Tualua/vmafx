- The `cpu` clang-tidy lane and the changed-files job measure the ten MATLAB MEX
  sources of `compat/python-vmaf/matlab/` against self-authored stub `mex.h` and
  `matrix.h` (`scripts/ci/lint-stubs/matlab/`); their lint exceptions are removed.
  The first measurement fixed the mechanical findings and a defect in `ical_std.c`
  (`mxDestroyArray()` was called on a matrix's data pointer).
