- **The tester, Windows, macOS, production, operator / node and supply-chain
  workflows are built and smoke-tested before they publish
  ([ADR-1595](docs/adr/1595-pr-time-verify-push-only-workflows.md)).** A pull request
  that changes the tester image's or the Windows zip's inputs now builds the amd64
  image or the x64 zip; the macOS bundle is built weekly; the new `Release Dry Run`
  workflow builds the release images (no push) and the `vmaf-mcp` wheel, sdist and
  SBOMs on pull requests that touch their inputs and weekly. Nothing is pushed,
  signed or attested outside a release. See
  `docs/development/release-workflow-verification.md`.
