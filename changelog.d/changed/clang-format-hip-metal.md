- `clang-format` now reads `.hip` and `.metal` sources in the pre-commit hook, `make format`,
  `make format-check` and the native hook; the 18 kernel files that were not clean are
  formatted (line breaks only, device code unchanged). See
  `docs/development/pre-commit-hooks.md`, "clang-format reads `.hip` and `.metal`".
