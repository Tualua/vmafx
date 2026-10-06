- `scripts/dev/tidy-lane.sh --write` takes the baseline only from the run's own results.
  It copied `tidy-baseline-<lane>.json` from the shared report directory, so a run that
  wrote none could overwrite the checkout's baseline with another run's. A write that
  produces no baseline now leaves the file alone and exits non-zero.
