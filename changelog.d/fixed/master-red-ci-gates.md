- **CI gates that were red on master read the right inputs again.** The Meson
  test-entry-point contract no longer governs the `.ci/pelorus` checkout of another
  repository, the documentation freshness check installs `jsonschema` from its hash
  lock, `Tooling Tests` checks out the full history that the retired-ADR record check
  needs and its default-model gate test deletes each scratch copy of the tree, and
  `Tidy Changed` leaves out three C++-only headers that the CPU build cannot parse
  (`T-CI-MESON-CONTRACT-PELORUS-CHECKOUT-2026-10-06`,
  `T-CI-DOCS-FRESHNESS-JSONSCHEMA-2026-10-06`,
  `T-CI-TOOLING-TESTS-SHALLOW-AND-DISK-2026-10-06`,
  `T-CI-TIDY-CHANGED-CXX-HEADERS-2026-10-06`).
