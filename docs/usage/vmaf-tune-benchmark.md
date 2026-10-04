<!-- markdownlint-disable MD013 MD060 -->
# `vmaf-tune benchmark` — rank encoders from a corpus

`vmaf-tune benchmark` ranks the encoders in an existing corpus JSONL by the
lowest bitrate at which each one reaches a target VMAF. It reads the file
only: it runs neither FFmpeg nor libvmaf, so a report over a finished sweep
takes well under a second.

## Quick start

1. Build a corpus that covers every encoder you want to compare. The
   [`corpus` subcommand](vmaf-tune-corpus.md) writes one encoder per run, so
   run it once per encoder and concatenate the files, or use
   [`compare`](vmaf-tune-compare.md) for a bisect-based ranking.
2. Run the report:

    ```shell
    vmaf-tune benchmark \
        --from-corpus corpus.jsonl \
        --target-vmaf 92 \
        --baseline-encoder libx264 \
        --format markdown
    ```

The report has one row per encoder, best first. The default format is a
Markdown table, ready to paste into a PR comment.

## Flags

| Flag | Default | Meaning |
| --- | --- | --- |
| `--from-corpus JSONL` | required | Corpus file written by `vmaf-tune corpus`. |
| `--target-vmaf T` | `92.0` | Matched-quality threshold every encoder must clear. |
| `--baseline-encoder NAME` | lowest-bitrate encoder that clears the target | Encoder the bitrate-delta column is measured against. The run fails with exit code 2 when the name is not in the corpus. |
| `--format` | `markdown` | `markdown`, `json` or `csv`. |
| `--output PATH` | stdout | Write the report to a file. Parent directories are created; the path is echoed on stderr. |

Exit code is `0` on success and `2` for a missing corpus file, an unknown
baseline encoder, a corpus with no usable rows, or a malformed JSONL line.

## How a row is chosen

For each encoder, the command

1. keeps rows with `exit_status == 0` and a finite `vmaf_score` and
   `bitrate_kbps`;
2. picks the lowest-bitrate row whose `vmaf_score >= --target-vmaf`
   (ties go to the higher VMAF, then the lower CRF) and reports it with
   `status=ok`;
3. when no row clears the target, reports the highest-VMAF row (ties go to the
   lower bitrate) with `status=unmet`, so a CRF sweep that was too narrow is
   visible rather than dropped.

Rows are sorted `ok` first, then by ascending bitrate. The result covers
exactly the points in the corpus: if `libx264` was swept over 20 CRFs and
`libx265` over 3, the ranking reflects those samples only.

## Output formats

| Format | Use |
| --- | --- |
| `markdown` | PR comments and human review. |
| `json` | Notebooks, dashboards and follow-up automation. |
| `csv` | Spreadsheets and quick plots. |

Columns in the Markdown table: encoder, status, VMAF, kbps, delta kbps (percent
against the baseline), preset, CRF, row count, encode fps and score fps. The
CSV carries the same fields plus `target_vmaf`, `margin`, `source_count` and
`preset_count`. The JSON output is a list of objects with `encoder`,
`status`, `target_vmaf`, `margin`, `bitrate_kbps`, `bitrate_delta_pct`,
`rows`, `source_count`, `preset_count`, `encode_fps`, `score_fps` and a
`best` object (`src`, `preset`, `crf`, `vmaf_score`, `bitrate_kbps`,
`vmaf_model`).

`encode_fps` and `score_fps` are means over the encoder's rows, computed from
`duration_s`, `encode_time_ms`, `score_time_ms` and `framerate`; they are empty
when the corpus lacks those fields. Bitrate delta is empty when no encoder
clears the target and no baseline was given.

## History

- Phase G of the `vmaf-tune` workflow; design in
  [ADR-0424](../adr/0424-vmaf-tune-corpus-benchmark.md).

## See also

- [`vmaf-tune.md`](vmaf-tune.md) — overview of every subcommand.
- [`vmaf-tune-corpus.md`](vmaf-tune-corpus.md) — builds the corpus this
  command reads, including the JSON row schema.
- [`vmaf-tune-recommend.md`](vmaf-tune-recommend.md) — picks one CRF from the
  same corpus for a single encoder.
