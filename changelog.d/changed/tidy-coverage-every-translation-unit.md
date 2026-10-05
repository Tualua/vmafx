- **Every translation unit is read by a clang-tidy lane, or excepted by name.**
  The `cpu` lane now configures the embedded MCP server, a new `clang` lane
  builds the libFuzzer harnesses (which need clang), and a macOS `metal` lane
  reads the Objective-C++ Metal host code with Homebrew's clang-tidy 22 against
  the Xcode SDK. The check that fails when a tracked
  translation unit is in no lane and not excepted, and the exception entries
  for the units no lane can read, land in the follow-up pull request.
  `tidy-ratchet.py` gains `--select` (measure one part
  of the tree) and a scoped write now records the files it measures. The
  embedded MCP server and the fuzz harnesses end at zero findings. See
  [docs/development/tidy-lanes.md](docs/development/tidy-lanes.md).
