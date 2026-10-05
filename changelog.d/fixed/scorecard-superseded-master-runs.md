- **A Scorecard master run whose master moved on during the scan ends
  cancelled, not failed.** The `Scorecard Master Gate` refused every report
  once the live master ref no longer named the scanned commit, so a merge-train
  landing during a scan turned master red although nothing was wrong (run
  37276297218: master `b0d991df7` read at `53e582831`, one commit ahead, report
  score 8.7). The gate now compares the two commits through GitHub's compare
  API: a final ref that descends from the scanned commit makes the run
  superseded, and the gate writes a receipt, a step summary and a notice naming
  the newer commit, then cancels its own run, so the run shows neither a pass
  nor a failure and the newer commit's push run gives the verdict. An invalid
  final ref or a move that is not to a descendant still fails. The gate job
  holds `actions: write` for the cancel and runs on `!cancelled()` instead of
  `always()`
  ([ADR-1686](docs/adr/1686-scorecard-superseded-master-runs.md)).
