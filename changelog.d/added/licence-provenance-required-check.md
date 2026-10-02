- **Required check `Licence Provenance`.** Every pull request and every push
  to `master` now runs `scripts/dev/relicense_fork_files.py --check`
  (ADR-1250): a fork-authored file with another licence, a file that
  reproduces upstream code without that code's notice, and a stale entry in the
  reviewed provenance file fail the merge gate. The upstream tree it compares
  against is the Netflix/vmaf commit the repository records as the head it is
  at parity with (`docs/development/known-upstream-bugs.md`, read by the new
  `scripts/ci/upstream_parity_pin.py`), so a commit pushed upstream cannot turn
  the check red on an unrelated pull request. The tool refuses a shallow
  checkout. Guide:
  [docs/development/licence-provenance-check.md](docs/development/licence-provenance-check.md)
  ([ADR-1474](docs/adr/1474-relicense-helper-headers-and-ci-check.md),
  closes `T-RELICENSE-CHECK-PENDING-2026-10-02`).
