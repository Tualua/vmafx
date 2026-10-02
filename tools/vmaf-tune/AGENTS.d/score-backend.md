---
paths:
  - tools/vmaf-tune/src/vmaftune/score*.py
  - tools/vmaf-tune/tests/test_score_backend*.py
invariant: Score backend auto-selection is native-first; selection strict by default; duration_s is decode clamp.
---
<!-- markdownlint-disable MD024 -->
# Score backend selection and routing

- **Score backend auto-selection is native-first (ADR-0667 /
  ADR-0726).** `vmaftune.score_backend.DEFAULT_FALLBACKS` must stay
  `cuda -> sycl -> hip -> cpu`. ADR-0726 (2026-05-28) removed Vulkan
  from chain. Adding another explicit backend to `ALL_BACKENDS`
  requires same-PR probe, docs update, and strict-mode unit tests.
- **Score backend selection is strict-by-default
  ([ADR-0299](../../../docs/adr/0299-vmaf-tune-gpu-score.md)).**
  `score_backend.select_backend(prefer)` honours `cuda` / `sycl` /
  `hip` / `cpu` exactly — if requested backend not available, it
  raises `BackendUnavailableError` rather than silently falling
  back to CPU. Only `prefer="auto"` walks fallback chain. Do not
  "fix" strict-mode test that fails on CI runner without GPU by
  adding silent fallback to `select_backend`; strict guarantee is
  load-bearing for operator wall-clock expectations. Mock
  `available` argument or `runner` instead.
- **`--score-backend` argparse choices are kept in sync with
  `score_backend.ALL_BACKENDS` and libvmaf's `--backend NAME`
  vocabulary ([ADR-0314](../../../docs/adr/0314-vmaf-tune-score-backend-vulkan.md) /
  [ADR-0726](../../../docs/adr/0726-drop-vulkan-backend.md)).**
  Do NOT add new value (e.g. `metal`) to argparse `choices` tuple
  in `cli.py` without corresponding libvmaf-side wiring landing in
  same release. Four current values (`cpu`, `cuda`, `sycl`, `hip`)
  are exact set libvmaf CLI accepts post-ADR-0726 (Vulkan dropped
  2026-05-28); widening harness without widening binary produces
  silent strict-mode failures on hosts that probe positively for
  new value. Cross-reference: `core/tools/cli_parse.c` `--backend`
  alternation.

- **`ScoreRequest.duration_s` is decode-clamp, not score window
  (ADR-0498, Bug #v2-A).** New optional field threads down through
  `maybe_decode_distorted` / `_decode_to_raw_yuv` into ffmpeg `-t`
  output clamp. 10 s probe against 634 s source doesn't
  materialise tens of gigabytes of raw YUV. Score window is still
  driven by `frame_skip_ref` / `frame_cnt`; `duration_s` is purely
  disk-budget gate for container -> raw YUV decode step. Default
  `0.0` preserves legacy full-source decode.
