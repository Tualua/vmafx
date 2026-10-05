- The copyright and SPDX hook now reads `.hip`, `.metal`, `.mm`, `.pyx`, `.rs` and `.sh` as well,
  skips no path by name, and takes its exceptions from a declared list with a reason and an
  expiry per file (`.config/lint-exceptions.d/`, `docs/development/pre-commit-hooks.md`).
  `adm_dwt2_cy.pyx` gains its SPDX line.
