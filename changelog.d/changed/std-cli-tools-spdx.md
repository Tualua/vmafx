- **Four CLI tool files meet the lint standard and carry their SPDX line.**
  `core/tools/cli_parse.h`: `CLISettings` is ordered by alignment (36 bytes of
  padding before, 4 after; one analyzer finding), and the three C-shared
  `typedef`s are inside a cited `NOLINT` block. `core/tools/vidinput.h`: its
  eleven `typedef`s and `video_input_pixel_format` are one definition for the C
  readers and the C++ CLI, cited likewise. `core/tools/vmaf_bench.c` and
  `core/tools/vmaf_vpl.c` already measured zero and only gain the line; their
  stale allowances in the SYCL tidy baseline are removed. The CLI behaves the
  same: exit code, standard output, standard error and the output file are
  byte-identical for 76 invocations (every output format, `--precision`,
  `--model`, `--feature`, frame and backend flags, YUV and Y4M input, the
  error paths) under both program names, on x86-64 and on aarch64
  ([ADR-1142](docs/adr/1142-whole-codebase-standards.md),
  [ADR-1250](docs/adr/1250-eupl-fork-relicense.md)).
