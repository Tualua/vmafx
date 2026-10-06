- **`Cppcheck` is clean on the Metal host tests and shared math headers.** Seventeen
  findings are fixed (two by-value blocks carry a cited suppression because the
  headers are shared with Metal Shading Language, the rest are code changes); the six
  affected Metal host tests still pass (`T-CI-CPPCHECK-METAL-HOST-TESTS-2026-10-06`).
