- Three pull-request checks now block a merge (ADR-1687): `Tester Image` (the
  amd64 tester image build and test), `Windows Tester Zip` (the x64 zip build,
  verify and SBOM) and `Release Dry Run` (the release images and the `vmaf-mcp`
  distribution and SBOM, nothing published). The two tester workflows start on
  every pull request and master push and build only when the CI impact planner
  selects their inputs (`tester_image`, `windows_tester_zip` in
  `.github/ci-impact.json`, the former path filters); a run that builds must
  pass. Their source-validation jobs are now named `Validate tester image source`
  and `Validate Windows zip source`. See
  `docs/development/release-workflow-verification.md`.
