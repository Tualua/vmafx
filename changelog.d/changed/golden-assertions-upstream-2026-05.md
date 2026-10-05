- **Netflix golden assertions: Netflix's own 2026-04/05 re-records ported verbatim
  (ports of Netflix/vmaf `5c7770080`, `005988ead`, `4679db83c`, `d93495f5c`,
  `e3827e4dd`, [ADR-1828](docs/adr/1828-port-netflix-golden-updates.md)).**
  162 expected values and their `places` in `python/test/quality_runner_test.py`,
  `result_test.py`, `routine_test.py`, `local_explainer_test.py` and
  `vmafexec_test.py` now read exactly as upstream has them; the fork's CPU
  build reproduces all 156 exercised values at upstream's places. The rule
  against editing golden assertions now names this one exception: Netflix's own
  update, copied verbatim after a measurement, never a fork-chosen value.
