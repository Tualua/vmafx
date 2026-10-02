---
paths:
  - core/test/test_output.c
  - core/test/test_public_api_score.c
  - core/test/test_output_open_eintr.c
invariant: Output tests use tmpfile() + slurp; tests needing named temp file resolve temp directory at runtime.
---
<!-- markdownlint-disable MD013 -->
# Output writer tests, temp files, and EINTR control

- **Output / writer-format tests use `tmpfile()` + slurp.**
  [`test_output.c`](../test_output.c) is reference for exercising
  `vmaf_write_output_{xml,json,csv,sub}` (R3 of
  [coverage gap analysis](../../../docs/development/coverage-gap-analysis-2026-05-02.md)):
  open `tmpfile()`, run writer, `fseek(SEEK_END)` + `ftell` +
  `fseek(SEEK_SET)` + `fread` to slurp buffer, then `strstr` for
  expected markers.
- **Tests that need *named* temp file (path-on-disk dispatch)** must
  resolve temp directory at runtime — never hardcode `/tmp/...`
  with `mkstemp(3)`. MSYS2/MinGW64 inside GitHub Actions
  `windows-latest` runner does not expose usable `/tmp` from
  `MINGW64` shell. `mkstemp` against `/tmp/foo_XXXXXX` template
  fails with `ENOENT`; test wedges Windows matrix leg red
  (ADR-0515 history: `test_public_api_score::test_vmaf_write_output`).
  Reference patterns: `make_temp_output_path()` helper in
  [test_public_api_score.c](../test_public_api_score.c) and inline
  `#ifdef _WIN32 ... GetTempPathA ... #else mkstemp ... #endif` block
  in [dnn/test_model_loader.c](../dnn/test_model_loader.c)
  (`test_sidecar_parses`). Both use `<stdio.h>` `remove(path)`
  instead of `unlink(path)` so `<unistd.h>` doesn't have to be
  pulled in on Windows. To reach `vmaf_feature_score_pooled`, test
  must use real `VmafContext` (writers require it for
  `pooled_metrics` block); obtain owned collector through
  `core/src/libvmaf_priv.h::vmaf_feature_collector_get()` and link
  against libvmaf. Do not include `libvmaf.c` / `output.c` directly
  from `test_output.c`: Apple ld64 + LTO has resolved that
  duplicate-definition pattern incorrectly under allocator
  poisoning, causing macOS writer-test SIGSEGVs. `test_output` still
  needs private-symbol access, so its Meson target disables LTO on
  Darwin only; Linux clang must keep LTO enabled at link time
  because `src/libvmaf.a` contains LLVM bitcode in clang builds.
  Public ABI tests that do not need private symbols must link
  `libvmaf_public_link` so `default_library=both` exercises shared
  library instead of Apple ld64's static-LTO path.
  **Pooled-metrics invariant**: for writer to emit per-feature
  mean/min/max/harmonic_mean entries, *every* index in
  `[0, pic_cnt)` must have written value for every feature.
  `vmaf_feature_score_pooled` returns `-EAGAIN` on first missing
  index; writer skips that feature. Sparse-frame branches
  (`count_written_at == 0`, `i > capacity`) belong in CSV / SUB
  tests where pic_cnt isn't precondition.

- [ADR-0515](../../../docs/adr/0515-test-public-api-score-mingw64-temp-path.md) —
  MinGW64 portable temp-path: no hardcoded `/tmp/` + `mkstemp`; use
  `make_temp_output_path()` pattern (`GetTempPathA` on `_WIN32`,
  `mkstemp` on POSIX). **Rebase-sensitive**: any new test that needs
  named temp file must follow this pattern or it will wedge
  `Build — Windows MinGW64 (CPU)` leg.

## Output-file `EINTR` fault control (Research-2084)

`test_output_open_eintr.c` is Linux static-link control for
`core/src/libvmaf.c::output_file_open()`. project defines large-file
support, so production `open(2)` reference reaches linker as
`open64`; test must wrap that actual symbol and inject `EINTR` exactly
once for its target path. Its Meson target is restricted to non-shared,
non-LTO Linux build because whole-program optimization or shared-library
boundary defeats GNU ld `--wrap`. Preserve assertions that write
succeeds and exactly two target opens occurred. passing write with zero
wrapped calls is not evidence. See
[Research-2084](../../../docs/research/2084-dev-mcp-resilience-restoration.md).
