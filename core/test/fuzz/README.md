# libFuzzer harnesses

Per-target [libFuzzer](https://llvm.org/docs/LibFuzzer.html) harnesses
for parser and decoder surfaces inside libvmaf. Tracked under
[ADR-0270](../../../docs/adr/0270-fuzzing-scaffold.md); operator runbook
in [`docs/development/fuzzing.md`](../../../docs/development/fuzzing.md).

## Targets

| Harness            | Surface under test                                                                                            | Corpus                |
|--------------------|---------------------------------------------------------------------------------------------------------------|-----------------------|
| `fuzz_y4m_input`   | `video_input_open` / `_fetch_frame` (Y4M parser, `core/tools/y4m_input.c`)                                    | `y4m_input_corpus/`   |
| `fuzz_yuv_input`   | `raw_input_open` / `_fetch_frame` (raw YUV reader, `core/tools/yuv_input.c`)                                  | `yuv_input_corpus/`   |
| `fuzz_cli_parse`   | `cli_parse` argv tokeniser + colon-delimited model/feature parsers (`core/tools/cli_parse.cpp`)               | `cli_parse_corpus/`   |
| `fuzz_json_model`  | `vmaf_read_json_model_from_buffer` + collection variant (SVM model JSON parser, `core/src/read_json_model.c`) | `json_model_corpus/`  |
| `fuzz_dnn_sidecar` | `vmaf_dnn_sidecar_load` (tiny-AI sidecar JSON parser, `core/src/dnn/model_loader.c`)                          | `dnn_sidecar_corpus/` |

## Build

The harnesses are opt-in. They require `clang` (gcc does not ship
libFuzzer) and pair best with AddressSanitizer:

```bash
CC=clang CXX=clang++ \
  meson setup build-fuzz core \
    --buildtype=debug \
    -Db_sanitize=address \
    -Db_lundef=false \
    -Db_lto=false \
    -Dfuzz=true \
    -Denable_cuda=false -Denable_sycl=false
ninja -C build-fuzz test/fuzz/fuzz_y4m_input
```

## Run a smoke fuzz locally

```bash
./build-fuzz/test/fuzz/fuzz_y4m_input \
    -max_total_time=60 \
    -rss_limit_mb=2048 \
    core/test/fuzz/y4m_input_corpus/
```

A clean run prints `Done <N> runs in <60> second(s)` with no
crash artefacts. If the fuzzer prints `==<pid>==ERROR:
AddressSanitizer ...` and writes a `crash-<sha>` file in the working
directory, treat that as a real bug — triage with the reproducer:

```bash
./build-fuzz/test/fuzz/fuzz_y4m_input crash-<sha>
```

## Corpus management

Seed inputs live under `<target>_corpus/` and are committed verbatim.
Keep them small (<= 1 KiB each) and biased toward branch coverage
(one per chroma sampling type, one per pixel-depth path, etc.).
The fuzzer extends the corpus at runtime in its working directory —
do **not** commit those discoveries unless they reproduce a crash
that motivates a new seed.

## Known crashes

Reproducers for **already-reported** crashes live under
`<target>_known_crashes/`. They are deliberately **excluded** from
the nightly CI job (running them would abort the run before the
fuzzer makes meaningful progress) but are kept in-tree so a future
regression is caught the moment a fixing PR re-enables them as
seeds. Each filename encodes the failing branch in shorthand
(e.g. `y4m_411_w2_h4_oob_dst.y4m` = 411-chroma path, `W2 H4`,
out-of-bounds destination write). Run them manually after a fix
lands:

```bash
for f in core/test/fuzz/y4m_input_known_crashes/*.y4m; do
    ./build-fuzz/test/fuzz/fuzz_y4m_input "$f"
done
```

Today the directories that exist are `y4m_input_known_crashes/` and
`json_model_known_crashes/`. `fuzz_cli_parse` has no known-crash directory and
no early-reject filter: its one historical crash (a getopt abbreviation such as
`--th=foosoxe` reaching `parse_unsigned`) is fixed, and the reproducer
`cli_parse_corpus/cli_threads_abbrev_assert.argv` is a regular seed that the
nightly run exercises ([ADR-0311 §Consequences](../../../docs/adr/0311-libfuzzer-harness-expansion.md#consequences)).

The `fuzz_json_model` harness surfaced a heap-buffer-overflow in
`vmaf_model_destroy`: `parse_slopes` grew the feature array via
`ensure_feature_capacity` while leaving `n_features` unchanged, so the
destroy walk and the cross-key validation disagreed. It is fixed
(`sync_n_features()` in `core/src/read_json_model.c`,
[ADR-0887](../../../docs/adr/0887-vmaf-model-slopes-feature-mismatch-validation.md)).
The reproducer stays in `json_model_known_crashes/slopes_oob_destroy.bin` (the
`.bin` extension keeps the `pre-commit check-json` hook from trying to parse
fuzzer-mutated bytes that are deliberately not valid JSON) as a regression
input; per [ADR-0404](../../../docs/adr/0404-nightly-fuzz-triage-keep-gates.md)
the nightly gate was kept running unmodified until the fix landed.

## CI

The nightly [`.github/workflows/fuzz.yml`](../../../.github/workflows/fuzz.yml)
job runs each harness for 60 seconds against the seed corpus and
uploads any `crash-*` / `oom-*` / `timeout-*` artefacts. It is the
gate that satisfies the OSSF Scorecard
[`Fuzzing`](https://github.com/ossf/scorecard/blob/main/docs/checks.md#fuzzing)
check.

## Add a new harness

1. Drop `fuzz_<target>.c` here that defines
   `int LLVMFuzzerTestOneInput(const uint8_t *, size_t)`.
2. Add a small seed corpus directory under `<target>_corpus/`.
3. Add an `executable('fuzz_<target>', …)` block in
   [`meson.build`](meson.build) using the same `fuzz_flags` pattern.
4. Add a row to the `## Targets` table above.
5. Add the new target to
   [`.github/workflows/fuzz.yml`](../../../.github/workflows/fuzz.yml)
   matrix so nightly CI exercises it.
