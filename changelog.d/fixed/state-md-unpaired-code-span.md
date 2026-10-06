- **The markdown governance gate is back to seconds on `docs/state.md`.** Two
  ledger rows left a backtick unpaired; because most of the ledger is one
  paragraph, every later code span re-paired and a `[` fell outside any span,
  which made the GFM autolink-literal parser behind the gate quadratic and the
  lint of that file exceed the gate's 120 s budget. The rows are repaired, and
  `scripts/ci/check-state-md-rows.sh` now refuses a line of `docs/state.md`
  whose code spans or `[` brackets do not close on that line.
