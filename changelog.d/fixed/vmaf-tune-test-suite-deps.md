- **`vmaf-tune`'s test suite passes from a plain `pip install -e
  "tools/vmaf-tune[dev]"`.** The `dev` extra now holds matplotlib and ONNX
  Runtime, which `vmaf-tune report` and the ONNX-backed features import but
  the package declared nowhere, and new extras `report`, `onnx` and `train`
  name them for users. Four tests that pinned the old `vmaf.c` /
  `cli_parse.c` sources, ran an upstream `vmaf` from `PATH`, or swallowed
  their own failure now test the current code, and each test runs in its own
  working directory, so the suite no longer leaves a `-version` file behind.
