---
paths:
  - core/src/feature/hip/integer_adm/adm_dwt2.hip
  - core/src/feature/hip/integer_adm/adm_csf.hip
invariant: Extern C macro instantiation pattern is required for HIP kernel exports.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# extern "C" macro-instantiation pattern is correct (Research-0755)

Several ADM kernel files (`adm_csf.hip`, `adm_csf_den.hip`,
`adm_dwt2.hip`) define `__global__` kernel bodies inside `#define`
macros, then instantiate those macros inside `extern "C" { }` block.
Correct: C++ preprocessor expands macro at point of instantiation
(inside `extern "C"`), so resulting function definition unmangled,
`hipModuleGetFunction` name lookups work. NOT an `extern "C"` gap.

Pattern is load-bearing. Do not "fix" it by adding additional
`extern "C"` declaration inside macro body — would create nested
`extern "C"` which is legal in C++ but redundant and confusing to
reviewers.
