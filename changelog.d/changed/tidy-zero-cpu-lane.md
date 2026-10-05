- The clang-tidy `cpu` lane baseline is empty (70 findings in 23 files to 0).
  C headers shared by C and C++ translation units carry cited `NOLINT` blocks
  (ADR-1138, ADR-1470), `cJSON.h` macros parenthesise their arguments and
  `isnumeric()` no longer reads a `string_view` as a C string. No score or API change.
