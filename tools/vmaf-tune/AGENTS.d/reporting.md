---
paths:
  - tools/vmaf-tune/src/vmaftune/report.py
  - tools/vmaf-tune/src/vmaftune/benchmark.py
  - tools/vmaf-tune/tests/test_report*.py
invariant: Report JSON carries encoder-profile contract; profile-card --format both emits 3 artifacts; sweep chart draw order.
---
<!-- markdownlint-disable MD024 -->
# Reports, profile cards, and benchmark

- **Phase G benchmark is read-only corpus analysis (ADR-0424).**
  `vmaf-tune benchmark` consumes existing Phase-A JSONL rows and
  must not call `ffmpeg`, `vmaf`, `compare.compare_codecs`, or
  Phase-B bisect. Its contract is one summary row per encoder:
  lowest-bitrate corpus point clearing `--target-vmaf`, with
  closest misses preserved as `status="unmet"`. Live encode
  comparisons stay in `compare`; offline corpus reports stay in
  `benchmark`.

- **Profile-card reports start with run-specific takeaways.**
  `report.py::_quick_takeaways` is single source for Markdown and
  HTML "Quick takeaways" section (ADR-0666). It must stay derived
  from `ReportData`, not from rendered text or browser-side
  JavaScript, so Markdown, HTML, tests, and future PDF/export
  paths agree on same recommendation summary.

- **`_run_report` separates infrastructure-gap rows from real
  failures (ADR-0501, Bug #V4-C).** Row whose `error` starts with
  `"encoder unavailable"` (bisect discriminator prefix added in
  ADR-0498 follow-up #6) raises new top-level `degraded=true` flag
  without flipping `ok=false`. `ok=true` requires
  `at_least_one_row_succeeded AND no_real_failure`; `ok=false`
  whenever any non-unavailable row fails. New counter
  `codec_rows_unavailable` exposes gap count for dashboards.
  Changing prefix in `bisect._predicate_for_codec` must update
  this aggregator too.
- **Report JSON carries encoder-profile contract (ADR-0643).**
  `ReportData.to_dict()` embeds
  `encoder_profile.schema == "vmaftune.encoder_profile.v1"` and
  `vmaf-tune encode-profile` reads that payload from JSON, HTML,
  or Markdown reports. Do not drop successful rows, failed rows,
  codec metadata, source geometry, `pix_fmt`, binary provenance,
  or `selected_pareto`; profile reader uses them to choose exactly
  one recommendation and construct FFmpeg argv. HTML escaping of
  raw JSON `<pre>` is part of contract because reader unescapes it
  before `json.loads`.
- **Profile-card `--format both` means all three artifacts.** Both
  `compare` and `report` call
  `report.write_report_outputs`; do not restore CLI-local writer copies.
  The helper emits machine-readable `.json` before the `.html` and `.md`
  renders, and returns those paths in that order.
  `--json-sidecar` adds JSON to a single-format HTML or Markdown run;
  it is not required for `both`. Regression tests must call the
  production writer or CLI entry point rather than copy its dispatch
  logic into a test helper.

- **Sweep chart draw order** (`report.py`, `_sweep_plot_fn._plot`):
  codec curves, then `_draw_sweep_pareto`, then
  `_draw_sweep_failures`, then `_style_sweep_axes`. Matplotlib
  z-order plus `_style_sweep_axes` reading finished artist list via
  `get_legend_handles_labels` both depend on this. Reordering
  silently drops legend entries.
