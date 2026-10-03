- **Upstream parity guard: `make upstream-parity` compares this tree's CPU
  extractors with Netflix/vmaf at the recorded parity head, every emitted
  value at `%.17g`** (ADR-1487). It builds both trees in the dev container
  image, where every comparison is made (elsewhere the guard refuses, or
  with `--unpinned` reports an advisory verdict), runs 16 shared extractors,
  their option variants and the shipped models on the scalar path and the
  default dispatch, and fails on a difference that no recorded deviation
  covers, on one larger than its recorded bound, and on a recorded deviation
  that no longer exists. `make upstream-parity-full` runs the whole matrix
  twice, the second time with the heap filled, and fails on an output of
  this tree that changes or on a finite bound over an upstream value that
  does. The deviations are fragments under `scripts/ci/upstream_parity.d/`,
  listed in `docs/development/upstream-parity-allowlist.md`; the guide is
  `docs/development/upstream-parity.md`.
