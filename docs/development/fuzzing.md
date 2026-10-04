<!-- markdownlint-disable MD060 -->
# Fuzzing libvmaf

Operator runbook for the libFuzzer harnesses under
[`core/test/fuzz/`](../../core/test/fuzz/): build them with clang, run one for
60 seconds, and triage a crash. Five harnesses ship, and a nightly CI job runs
all of them.

## Quick run

1. Build the harnesses (clang only; see [Build the harness](#build-the-harness)
   for what each flag does):

    ```bash
    CC=clang CXX=clang++ \
      meson setup build-fuzz core \
        --buildtype=debug \
        -Db_sanitize=address \
        -Db_lundef=false \
        -Db_lto=false \
        -Dfuzz=true \
        -Denable_cuda=false -Denable_sycl=false
    ninja -C build-fuzz test/fuzz/fuzz_y4m_input \
                        test/fuzz/fuzz_yuv_input \
                        test/fuzz/fuzz_cli_parse \
                        test/fuzz/fuzz_json_model \
                        test/fuzz/fuzz_dnn_sidecar
    ```

2. Run one harness for 60 seconds against its seed corpus (see
   [Run a 60-second smoke](#run-a-60-second-smoke)):

    ```bash
    mkdir -p /tmp/fuzz-smoke-y4m
    ./build-fuzz/test/fuzz/fuzz_y4m_input \
        -max_total_time=60 -rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=10 \
        /tmp/fuzz-smoke-y4m core/test/fuzz/y4m_input_corpus/
    ```

3. If it aborts with an AddressSanitizer error and writes a `crash-<sha>`
   file, replay that file and follow
   [Triage a crash](#triage-a-crash).

The decisions behind the harnesses are recorded in
[ADR-0270](../adr/0270-fuzzing-scaffold.md) (initial scaffold),
[ADR-0311](../adr/0311-libfuzzer-harness-expansion.md) (`fuzz_yuv_input` and
`fuzz_cli_parse` expansion) and
[ADR-0882](../adr/0882-fuzz-target-audit-json-model-sidecar.md)
(`fuzz_json_model` and `fuzz_dnn_sidecar` audit). The harnesses satisfy the
OSSF Scorecard
[`Fuzzing`](https://github.com/ossf/scorecard/blob/main/docs/checks.md#fuzzing)
check.

## What is shipped

| Harness | Surface | Known crashes |
|---------|---------|---------------|
| `fuzz_y4m_input` | YUV4MPEG2 parser (`video_input_open` / `_fetch_frame` / `_close`) | 2 reproducers: the 411-chroma OOB write (ADR-0270 §Consequences) and a negative-width NULL dereference, fixed by [ADR-0382](../adr/0382-y4m-neg-dimension-rejection.md) and kept as a regression seed. |
| `fuzz_yuv_input` | Headerless raw-YUV reader (`raw_input_open` / `_fetch_frame`) | 0 |
| `fuzz_cli_parse` | `cli_parse` argv tokeniser and the colon-delimited `--feature` / `--model` parser | 1: a `--threads=<garbage>` abbreviation tripping an `error()` assert (ADR-0311 §Consequences). No reproducer directory is in the tree. |
| `fuzz_json_model` | `vmaf_read_json_model_from_buffer` and its collection variant (SVM model JSON parser, `core/src/read_json_model.c`) | 1: `parse_slopes` outruns `feature_names`, then `vmaf_model_destroy` writes out of bounds (ADR-0882, T-JSON-MODEL-SLOPES-FEATURE-CAP-OOB-2026-05-30). |
| `fuzz_dnn_sidecar` | `vmaf_dnn_sidecar_load` (tiny-AI sidecar JSON parser, `core/src/dnn/model_loader.c`) | 0 |

Sources and seed corpora, all under `core/test/fuzz/`:

| Harness | Source | Seed corpus (files) |
|---------|--------|---------------------|
| `fuzz_y4m_input` | [`fuzz_y4m_input.c`](../../core/test/fuzz/fuzz_y4m_input.c) | [`y4m_input_corpus/`](../../core/test/fuzz/y4m_input_corpus/) (13) |
| `fuzz_yuv_input` | [`fuzz_yuv_input.c`](../../core/test/fuzz/fuzz_yuv_input.c) | [`yuv_input_corpus/`](../../core/test/fuzz/yuv_input_corpus/) (6) |
| `fuzz_cli_parse` | [`fuzz_cli_parse.c`](../../core/test/fuzz/fuzz_cli_parse.c) | [`cli_parse_corpus/`](../../core/test/fuzz/cli_parse_corpus/) (8) |
| `fuzz_json_model` | [`fuzz_json_model.c`](../../core/test/fuzz/fuzz_json_model.c) | [`json_model_corpus/`](../../core/test/fuzz/json_model_corpus/) (7) |
| `fuzz_dnn_sidecar` | [`fuzz_dnn_sidecar.c`](../../core/test/fuzz/fuzz_dnn_sidecar.c) | [`dnn_sidecar_corpus/`](../../core/test/fuzz/dnn_sidecar_corpus/) (5) |

New harnesses follow the README at
[`core/test/fuzz/README.md`](../../core/test/fuzz/README.md).

## Build the harness

The fuzz harnesses are opt-in and require **clang**, because libFuzzer is a
clang-only feature. They pair best with AddressSanitizer. The build command is
step 1 of the [quick run](#quick-run).

Three non-default Meson flags are load-bearing:

| Flag | Why |
|------|-----|
| `-Dfuzz=true` | Opts the `core/test/fuzz/` subdirectory into the build (default `false`). |
| `-Db_lundef=false` | Clang's libFuzzer runtime defines symbols that resolve at final-link time; the default `b_lundef=true` errors them out at setup. The harness `meson.build` emits a clear warning at setup time if this is forgotten. |
| `-Db_lto=false` | The `fuzz_json_model` and `fuzz_dnn_sidecar` harnesses (ADR-0882) compile parser sources directly into the harness binary. With LTO on, ASan's module-dtor sections are discarded at link time on the larger source set, producing a hard linker error. |

## Run a 60-second smoke

Each harness is independent: pick one, or run several back-to-back. The
invocation is the same for all five; only the binary, the scratch directory and
the seed corpus change.

```bash
mkdir -p /tmp/fuzz-smoke-y4m /tmp/fuzz-smoke-yuv /tmp/fuzz-smoke-cli \
         /tmp/fuzz-smoke-json /tmp/fuzz-smoke-dnn

./build-fuzz/test/fuzz/fuzz_y4m_input \
    -max_total_time=60 -rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=10 \
    /tmp/fuzz-smoke-y4m core/test/fuzz/y4m_input_corpus/

./build-fuzz/test/fuzz/fuzz_yuv_input \
    -max_total_time=60 -rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=10 \
    /tmp/fuzz-smoke-yuv core/test/fuzz/yuv_input_corpus/

./build-fuzz/test/fuzz/fuzz_cli_parse \
    -max_total_time=60 -rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=10 \
    /tmp/fuzz-smoke-cli core/test/fuzz/cli_parse_corpus/

# fuzz_json_model: CI caps it at 512 MB RSS because it reached 1.1 GB
# peak RSS in 180-second runs.
./build-fuzz/test/fuzz/fuzz_json_model \
    -max_total_time=60 -rss_limit_mb=512 -malloc_limit_mb=1024 -timeout=10 \
    /tmp/fuzz-smoke-json core/test/fuzz/json_model_corpus/

./build-fuzz/test/fuzz/fuzz_dnn_sidecar \
    -max_total_time=60 -rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=10 \
    /tmp/fuzz-smoke-dnn core/test/fuzz/dnn_sidecar_corpus/
```

Expected output on a clean run:

```text
INFO: Running with entropic power schedule (0xFF, 100).
…
Done <N> runs in 60 second(s)
```

## Triage a crash

If a harness aborts with `==<pid>==ERROR: AddressSanitizer …` and writes a
`crash-<sha>`, `oom-<sha>` or `timeout-<sha>` file in the working directory,
treat that as a real bug.

1. Re-run the single artefact for a clean stack trace:

    ```bash
    ./build-fuzz/test/fuzz/fuzz_y4m_input crash-<sha>
    ```

2. File the bug per the [bug-tracking workflow in `docs/state.md`](../state.md).
3. Park the reproducer under `core/test/fuzz/<target>_known_crashes/` (see
   [`core/test/fuzz/README.md` § Known crashes](../../core/test/fuzz/README.md#known-crashes))
   so the regression is caught the moment the fix lands.

## Continuous fuzzing in CI

The [`fuzz.yml` GitHub Actions workflow](../../.github/workflows/fuzz.yml)
runs every harness in its own matrix job against the committed seed corpus and
uploads any crash, oom or timeout artefacts. It is the gate that satisfies the
Scorecard `Fuzzing` check.

| Setting | Value |
|---------|-------|
| Schedule | Nightly, 04:30 UTC |
| Time per target | 60 seconds by default (`MAX_TOTAL_TIME`) |
| Deeper run | Dispatch the workflow manually with a higher `max_total_time` input |
| RSS limit | 2048 MB per target; 512 MB for `fuzz_json_model` |

Adjust the duration through the workflow input or `MAX_TOTAL_TIME`, not by
editing the harness invocations.

## Adding a new harness

See the step list in
[`core/test/fuzz/README.md` § Add a new harness](../../core/test/fuzz/README.md#add-a-new-harness).
The summary:

1. Drop `fuzz_<target>.c` next to the existing harnesses.
2. Add an `executable(...)` block in
   [`core/test/fuzz/meson.build`](../../core/test/fuzz/meson.build).
3. Ship a small seed corpus under `<target>_corpus/`.
4. Register the target in the matrix in `.github/workflows/fuzz.yml`.
5. Update the tables at the top of this file.

## Known limitations

- The fuzz build is x86_64 / aarch64 with clang only. gcc has no libFuzzer;
  the Meson option errors cleanly when `cc.get_id()` is not `clang`.
- The harness caps input size at 64 KiB and rejects header lines whose `W` or
  `H` tag has more than 6 consecutive digits. This is a fuzzer-stability
  bound that keeps allocator-probe inputs from dominating the corpus. It is
  not a real-world cap on the parser.
- Real bugs reachable through unbounded dimensions are still in scope; the
  bound only avoids wasting fuzzer cycles on malloc-fragmentation paths.
- Coverage feedback is libFuzzer's intrinsic edge counter. No LCOV report is
  produced from fuzz runs; coverage is exercised separately by the unit-test
  gate.

## References

- [ADR-0270](../adr/0270-fuzzing-scaffold.md): decision matrix and rejected
  alternatives (OSS-Fuzz onboarding, AFL++, defer-until-OSS-Fuzz, driver-only
  psnr_y harness).
- [Research digest 0059](../research/0059-libfuzzer-scaffold-y4m.md): surface
  survey, smoke-run command, and the 411-chroma OOB finding.
- [libFuzzer (LLVM)](https://llvm.org/docs/LibFuzzer.html).
- [OSSF Scorecard `Fuzzing`
  check](https://github.com/ossf/scorecard/blob/main/docs/checks.md#fuzzing).
