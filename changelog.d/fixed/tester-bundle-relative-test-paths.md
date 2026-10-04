- **The macOS tester bundle's unit tests start on the tester's Mac**
  (`T-TESTER-BUNDLE-UNIT-PATHS-ABSOLUTE-2026-10-04`). The bundle's
  `image/unit-tests.json` named every test executable by its absolute path on
  the hosted runner that built it, so on any other Mac no unit test (and none of
  the Metal parity cases the state-row map reads) could start and the report's
  `unit_tests` section failed. Tester package manifests now name tests relative
  to the package root, and the report resolves them against the directory it
  runs from. The bundle published as `tester-20261003-c12763f3` has the defect;
  a newly published bundle does not.
