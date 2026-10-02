---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_cambi_hip.c
invariant: Prohibit path globs inside block comments to avoid tooling and lint false positives.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# No path globs in block comments

`core/src/feature/hip/*.c` inside `/* ... */` opens nested comment ->
`-Wcomment` on every HIP build -> zero-warning gate fails. 14 parity tests had
it. Name the set in prose: "the .c files under core/src/feature/hip/".
