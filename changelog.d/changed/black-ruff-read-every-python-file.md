- `black` and `ruff` now read every Python file in the tree in the pre-commit hooks, `make lint-py`,
  `make format` and `make format-check`, not only `python/ ai/ scripts/ tools/`. About 80 files were
  reformatted and the ruff findings fixed (no behaviour change; the syntax tree of the
  reformatted files is identical); the files that cannot meet a tool are declared with a reason
  and an expiry in `.config/lint-exceptions.d/` (`docs/development/pre-commit-hooks.md`).
