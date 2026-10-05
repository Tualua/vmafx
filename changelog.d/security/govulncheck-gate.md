- Go CI runs govulncheck at symbol level (`make govulncheck`,
  `scripts/ci/govulncheck-gate.py`, ADR-1899): a called vulnerable symbol fails
  the build, and an advisory whose code is required but never called needs an
  OpenVEX statement in `security/vex/go.openvex.json`. GO-2026-5932
  (`golang.org/x/crypto/openpgp`, no fixed version) is recorded as not affected:
  no vmafx binary compiles those packages.
