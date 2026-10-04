---
paths:
  - .github/workflows/release-dry-run.yml
  - scripts/ci/release-*.sh
invariant: Mirrors the release builds; publishes nothing.
---
<!-- markdownlint-disable MD013 MD060 -->
# Release dry run (ADR-1595)

- `release-dry-run.yml` runs the production, operator / server / node image
  builds (linux/amd64, no push), the GPU toolkit builds (no load) and the
  `vmaf-mcp` wheel, sdist and SBOMs on pull requests that touch their inputs and
  weekly. `release-dry-run-plan.sh` decides the groups; every path it names must
  exist (`test_plan_script_names_only_files_that_exist`).
- It holds no credential: no registry login, push, signature, attestation,
  environment or `id-token`. `test_is_a_dry_run` fails on any of them; a
  step that needs one belongs to the release workflow.
- `verify-mcp-sbom.sh` is the one SBOM check: `supply-chain.yml` (job `sbom`)
  and the dry run both call it, and `scripts/release/tests/test-verify-mcp-sbom.sh`
  plants one defect at a time. Do not inline its `jq` back into the workflow.
- The tester image and Windows zip workflows take a `pull_request` trigger with the
  paths of their `push` trigger; their `validate` job narrows the matrix on a pull
  request (amd64; x64) and never sets `publish=true` for it. The macOS bundle
  runs weekly. `test_pr_time_verify_workflows.py` executes the `validate` steps.
