---
paths:
  - core/test/dnn/test_cli.sh
  - core/tools/vmaf.cpp
invariant: Test CLI DNN feature probes require syntactically complete command lines before runtime checks execute.
---
<!-- markdownlint-disable MD013 -->
# DNN CLI Probing and Skipped Tests

## Invariant — test_cli.sh DNN probe must be a valid invocation

`core/test/dnn/test_cli.sh` skips (exit 77) when binary has no DNN
support. Probe has to be *otherwise valid* `vmaf` command line:
`configure_tiny_model()` in `core/tools/vmaf.cpp` runs after argument
validation and after inputs are opened, so bare
`vmaf --tiny-model /dev/null` dies on reference-required gate,
never reaches availability check. Probe therefore feeds real
`src01_hrc01` fixture through `--no-reference`. If availability
check ever moves earlier in `vmaf.cpp`, probe may be simplified —
until then, keep full command line.
