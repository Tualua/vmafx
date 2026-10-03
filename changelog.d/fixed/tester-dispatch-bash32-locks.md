- **The macOS tester bundle build runs under the hosted runner's bash 3.2, and the
  report schema check installs on Python 3.12.** `mapfile` in
  `scripts/ci/build-macos-tester-bundle.sh` is replaced by a loop; a contract test
  scans the macOS scripts for bash 4+ features and runs them under a real bash 3.2 when
  Docker is available. `requirements/locks/jsonschema.txt` is now a universal lock
  (`typing-extensions` for Python below 3.13). See
  [the maintainer notes](docs/development/tester-image.md).
