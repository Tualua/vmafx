- **Hardware we need: a processor row takes the verdict of the report's processor
  checks.** `scripts/docs/generate-hardware-reports.py` gave a CPU family row (for
  example "x86 with AVX2 and no AVX-512") the report's overall verdict, so a report
  whose CPU checks all passed but whose GPU section failed rated the processor row
  "worst fail". A CPU row now counts the dispatch and reference equivalence checks,
  the unit tests, the golden check and the image's file check; the native macOS row
  adds the Metal equivalence and the Metal gate, because it also closes the Metal
  rows. GPU rows keep their device's verdict. `CpuRowVerdictTests` in
  `scripts/docs/tests/test_hardware_needs.py` fails on the old generator.
