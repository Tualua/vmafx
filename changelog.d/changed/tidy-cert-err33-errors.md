- `cert-err33-c` (an ignored return value of a standard-library call) is in the
  `WarningsAsErrors` list of `.clang-tidy`. Every clang-tidy lane already measured
  zero findings of it, so the promotion ADR-0694 asked for changes no count; a new
  unchecked `fclose()` or `fputs()` now fails `Tidy Changed`.
