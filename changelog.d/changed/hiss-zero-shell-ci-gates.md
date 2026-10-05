- **The CI gate scripts meet the HISS shell rules (ADR-1142).** The CI gate
  scripts under `scripts/ci/` (ADR numbering, worktree drift, container and
  base-image checks, default-model and local-data contracts, state ledger
  checks, PR classification, release-PR exemption, twin drift, PR-body
  validation and the CUDA and oneAPI installers) accept a grep that finds
  nothing by its exit status and report any other failure, and every `curl` of
  the two installers has a connect and total time limit (`--connect-timeout`,
  `--max-time`). Each gate passes and fails on the same inputs as before. The
  HISS baseline loses 48 infractions.
