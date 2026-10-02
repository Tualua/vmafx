- **The `test-netflix-golden` target checks for pytest before execution.** When
  invoked in a fresh worktree where `.venv` only contains build-time dependencies,
  `make test-netflix-golden` previously stopped with `No module named pytest`.
  The target now checks for `pytest` availability up front and fails with an
  actionable error directing the developer to the documented install command in
  `docs/development/languages.md`.
