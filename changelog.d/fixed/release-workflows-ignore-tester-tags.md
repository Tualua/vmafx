- **A tester prerelease no longer starts the production release workflows.**
  `tester-*` prereleases (macOS and Windows tester bundles) fire the `release`
  event; `docker-publish-production.yml`, `docker-publish-operator-node.yml` and
  `supply-chain.yml` then failed at "Validate tag" and turned master red. Their
  first job now runs only for `v*` tags (workflow dispatch recovery unchanged),
  the rest skip with it, and a contract test covers every `on: release`
  workflow.
