- The praetor governance engine moves from `6c772713a133` to `0af07a733e65`
  (ADR-1506). The documentation gate's lock no longer contains `braces`
  (GHSA-vfj7-8cjw-p6xm), which clears the only finding of Scorecard's
  Vulnerabilities check. The gate also gains praetor's `Go API Compatibility`
  workflow. The engine now scans shell, workflow and systemd files, so the
  recorded debt figure rises from 227 to 503; the old engine records no growth
  on the same tree. Every workstation's `praetorctl` has to move to the new pin
  when this merges ([CI guide](docs/development/ci.md#moving-the-praetor-pin)).
