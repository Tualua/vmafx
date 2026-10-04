<!-- markdownlint-disable MD013 MD060 -->
# `vmaf-tune report` and `encode-profile`

`vmaf-tune report` turns the JSON that `compare`, `ladder` and
`tune-per-shot` produce into a profile card, and `vmaf-tune
encode-profile` runs one encode from a recommendation stored in that
card. Overview: [vmaf-tune.md](vmaf-tune.md).

## `report` — render a profile card

`report` reads one or more JSON dumps (`--compare-json`,
`--ladder-json`, `--per-shot-json`) and renders a profile card as HTML,
Markdown or both. The card starts with a Quick takeaways block that
summarises the concrete recommendation and the coverage gaps, followed by
the detailed tables. It also prints a one-line JSON summary on stdout for
`jq` or other automation.

```shell
vmaf-tune report \
    --src bbb_1080p_60fps.mp4 \
    --compare-json compare.json \
    --target-vmaf 92 \
    --format both \
    --output sweep_profile.html
```

`--format both` writes `.json`, `.html` and `.md` artifacts next to `--output`.
With
`--format html` or `--format markdown`, `--json-sidecar` adds the `.json`
file as well.

### Flags

| Flag | Default | Meaning |
|------|---------|---------|
| `--src PATH` | required | Reference video; ffprobe reads its metadata for the report header. |
| `--target-vmaf F` | `92.0` | Target VMAF shown in the report header. |
| `--compare-json PATH` | none | JSON from [`compare`](vmaf-tune-compare.md). |
| `--ladder-json PATH` | none | JSON from [`ladder`](vmaf-tune-ladder.md). |
| `--per-shot-json PATH` | none | JSON from [`tune-per-shot`](vmaf-tune-per-shot.md). |
| `--format {html,markdown,both}` | `html` | Artifact format. |
| `--output PATH` | required | Report destination; the `.html` or `.md` suffix matches `--format`. |
| `--assets-dir PATH` | none | With Markdown output, write the chart PNGs into this directory; without it they are embedded as base64. |
| `--json-sidecar` | off | Also write the `.json` artifact when the format is `html` or `markdown`. |
| `--pix-fmt`, `--preset`, `--score-backend`, `--ffmpeg-bin`, `--vmaf-bin` | unset | Values to record in the embedded encoder profile; empty by default. |

### Stdout summary

| Key | Meaning |
|-----|---------|
| `ok` | `true` when at least one codec row succeeded and no non-`ok` row is a real encode failure. An "encoder unavailable" row (codec not built into ffmpeg) does not flip this to `false`. With no codec rows the report is informational and stays `true`. |
| `degraded` | `true` when at least one codec row is an "encoder unavailable" row, so dashboards can show a missing codec without turning the run red. |
| `codec_rows` / `codec_rows_ok` / `codec_rows_failed` | Total, succeeded and failed row counts. |
| `codec_rows_unavailable` | Subset of `codec_rows_failed` whose `error` starts with `"encoder unavailable"`. |
| `ladder_samples` / `ladder_rungs` | Counts read from `--ladder-json`. `ladder_samples` reads the top-level `samples[]` array, always present in `vmaf-tune-ladder/v1` JSON. |
| `shots` | Number of per-shot rows read from `--per-shot-json`. |
| `outputs` | Paths of the rendered card files. |

`degraded` and `codec_rows_unavailable` were added with
[ADR-0501](../adr/0501-vmaf-tune-bbb-e2e-v4-bug-cluster.md). The
"encoder unavailable" test keys on the prefix
`"encoder unavailable (NAME): …"` that the bisect stage emits
([ADR-0498](../adr/0498-vmaf-tune-bbb-e2e-v2-bug-cluster.md)). A
row whose `error` lacks that prefix is a genuine encode or score failure
and sets `ok=false`.

## `encode-profile` — run one recommendation

Every HTML, Markdown and raw JSON profile card embeds an
`encoder_profile` payload with schema `vmaftune.encoder_profile.v1`
([ADR-0643](../adr/0643-vmaf-tune-encoder-profile-contract.md)). It holds
source metadata, tool and binary provenance, codec metadata, failures
and a sorted list of concrete encoder recommendations.

`encode-profile` reads that payload and runs exactly one selected
FFmpeg encode. It never encodes the whole ladder or every codec.

1. Print the exact FFmpeg command first with `--dry-run`:

    ```shell
    vmaf-tune encode-profile \
        --profile sweep_profile.html \
        --src bbb_1080p_60fps.mp4 \
        --codec libsvtav1 \
        --target-vmaf 96 \
        --output bbb_svtav1_vmaf96.mkv \
        --dry-run
    ```

2. Run the encode once the selected row looks right:

    ```shell
    vmaf-tune encode-profile \
        --profile sweep_profile.html \
        --src bbb_1080p_60fps.mp4 \
        --codec libsvtav1 \
        --target-vmaf 96 \
        --output bbb_svtav1_vmaf96.mkv \
        --extra-ffmpeg-arg=-movflags --extra-ffmpeg-arg=+faststart
    ```

### Selection rules

- With no filters, the first pareto-selected row with the lowest bitrate
  is used.
- `--codec NAME` narrows the candidates to one adapter token
  (`libsvtav1`, `libx265`, `av1_nvenc`, ...).
- `--target-vmaf F` narrows by the exact target stored in the profile.
- `--recommendation-index N` picks the zero-based row after those
  filters, useful when several rows stay tied or deliberately
  comparable.

### Flags

| Flag | Default | Meaning |
|------|---------|---------|
| `--profile PATH` | required | Report JSON, HTML or Markdown. |
| `--output PATH` | required | Encoded output path. |
| `--src PATH` | profile source | Override when the profile was generated on another machine. |
| `--source-kind {auto,container,raw}` | `auto` | `auto` treats `.yuv`, `.raw`, `.rgb` and `.gray` as raw and everything else as a container that FFmpeg detects. |
| `--width`, `--height`, `--framerate`, `--pix-fmt` | profile values | Required only when the selected source is raw and the profile lacks them. |
| `--preset P` | profile row or adapter default | Override the selected row's preset. |
| `--duration S` | profile source duration | Bounds the input-side encode window. |
| `--sample-clip-seconds N` / `--sample-clip-start-s S` | `0.0` | Optional input-side clip, forwarded to FFmpeg. |
| `--extra-ffmpeg-arg TOKEN` | none | Append one raw FFmpeg token after the codec arguments. Repeat as needed; write `--extra-ffmpeg-arg=-movflags` for tokens that start with a dash. |
| `--ffmpeg-bin PATH` | profile `ffmpeg_bin`, then `ffmpeg` | FFmpeg binary. |
| `--codec`, `--target-vmaf`, `--recommendation-index` | none | Selection filters, above. |
| `--dry-run` | off | Print the selected recommendation and `ffmpeg_argv` without encoding. |

## See also

- [Overview](vmaf-tune.md)
- [Compare](vmaf-tune-compare.md), [ladder](vmaf-tune-ladder.md),
  [per-shot](vmaf-tune-per-shot.md)
