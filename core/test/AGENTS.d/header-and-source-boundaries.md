---
paths:
  - core/test/test_dict.cpp
  - core/test/test_feature.cpp
  - core/test/test_flush_context_ordering.c
  - core/test/test_luminance_tools.cpp
  - core/test/test_bootstrap_name_contract.py
invariant: test_dict.cpp and test_feature.cpp are authoritative test files; test_bootstrap_name_contract.py is source-level test.
---
<!-- markdownlint-disable MD013 -->
# Header boundary smoke, test twins, and bootstrap names

- **Authoritative test twins (ADR-1153)**: `test_dict.cpp` and
  `test_feature.cpp` are sole authoritative test files for `dict`
  and `feature_name`; uncompiled legacy C twins `test_dict.c` and
  `test_feature.c` were deleted as obsolete.
- **Internal C/C++ header boundary smoke**: `test_flush_context_ordering.c` (C)
  and `test_luminance_tools.cpp` (C++) consume `cambi_internal.h`,
  `luminance_tools.h`, `model.h`, and `libvmaf_priv.h` directly and assert enum
  widths and ABI layout across language boundary without expanding
  authoritative CPU tidy translation unit inventory. Keep these dual-use header
  inclusions and static assertions when changing CodeQL link seams; unity
  includes formerly hid this boundary.

## Bootstrap score-name source contract (ADR-0480)

`test_bootstrap_name_contract.py` is intentionally source-level test. Public
score tests cannot detect whether `libvmaf.c` and `predict.c` have copied
same suffix literals away from `bootstrap_names.h`; both implementations can
remain behaviorally identical until later edit changes only one. Keep
test in `fast` suite and require both consumers to include header, use
all four shared symbols, and contain none of four literal definitions.
