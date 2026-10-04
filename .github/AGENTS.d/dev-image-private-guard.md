---
paths:
  - .github/workflows/dev-container-publish.yml
  - scripts/ci/require-private-ghcr-package.sh
  - scripts/ci/tests/test_require_private_ghcr_package.py
invariant: Dev image pushed only when GHCR says `private`; guard step first, unconditional, fail closed.
---
<!-- markdownlint-disable MD013 -->
# Dev image goes only into a private package (ADR-1564)

`ghcr.io/vmafx/vmafx-dev-mcp` = `libvmaf-build` stage: full CUDA toolkit,
Intel oneAPI Base Kit, ROCm payload. Licences allow internal use, not
redistribution. `dev-container-publish.yml` step "Refuse to push unless the
package is private" runs `require-private-ghcr-package.sh VMAFx vmafx-dev-mcp`
right after checkout, before Buildx, login and build-push. Script reads
`GET /orgs/VMAFx/packages/container/vmafx-dev-mcp` via `gh api` with the job
token; exit 0 only on `visibility == "private"` exactly. API error, 401/403/404,
missing or non-string `visibility`, `public`, `internal` -> exit 1. Never give
the step `if:` or `continue-on-error`, never move it after the push, never
add a tag outside that package to the build step: the test fails on each.
Test runs the workflow's own `run:` line against a stub `gh` (planted
`public` answer must fail). A token change that cannot read package metadata
makes the job fail: fix the token, not the guard.
