- `docs/state.md`: the two open Pelorus rows (the world-writable x265 fixture and the
  narrow `fopen` of the qp-report reader on Windows) are closed. Both were fixed in
  `VMAFx/pelorus` (issues #60 to #62) and are in the vendored mirror, which
  `scripts/sync-pelorus-interop.sh` reports as free of drift against a fresh clone.
