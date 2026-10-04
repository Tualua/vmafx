<!-- markdownlint-disable MD013 MD060 -->
# `vmaf-tune` Phase E — bitrate ladder

`vmaf-tune ladder` builds a per-title ABR ladder from sampled
`(resolution, target-VMAF)` cells. It scores the sampled points, builds the
Pareto frontier, picks a bounded number of rungs and writes an HLS, DASH or
JSON manifest. This page is the short introduction; the full flag table, the
manifest schema and the uncertainty extension are on
[`vmaf-tune-ladder.md`](vmaf-tune-ladder.md).

The implementation lives in `tools/vmaf-tune/src/vmaftune/ladder.py`, and the
CLI entry point is wired in `tools/vmaf-tune/src/vmaftune/cli.py`.

## Quick start

```shell
vmaf-tune ladder \
    --src ref.yuv \
    --encoder libx264 \
    --resolutions 1920x1080,1280x720,854x480 \
    --target-vmafs 95,92,88 \
    --quality-tiers 5 \
    --format hls \
    --output master.m3u8
```

`--resolutions` and `--target-vmafs` are required. Add `--framerate` and
`--duration` that match the source: `--duration` defaults to `1.0` second and
divides the encode size into the manifest bitrate.

## Pipeline

1. `build_ladder()` samples every `(resolution, target_vmaf)` pair.
2. The default sampler encodes and scores the 5-point CRF sweep
   `20,25,30,35,40` at that resolution (override with `--crf-sweep`) and keeps
   the row that meets the target at the smallest CRF.
3. `convex_hull()` removes dominated bitrate and quality points.
4. `select_knees()` chooses the requested number of rungs along the hull.
5. `emit_manifest()` writes HLS, DASH or JSON.

Programmatic callers can pass `sampler=` to `build_ladder()` for a custom grid,
a precomputed corpus or a bisect-backed sampler.

## Common flags

| Flag | Default | Meaning |
| --- | --- | --- |
| `--src PATH` | required | Source clip. |
| `--resolutions LIST` | required | Comma-separated `WxH` list. |
| `--target-vmafs LIST` | required | Comma-separated VMAF targets. |
| `--quality-tiers N` | `5` | Number of final rungs. |
| `--format` | `hls` | `hls`, `dash` or `json`. |
| `--spacing` | `log_bitrate` | `log_bitrate`, `vmaf`, or `uniform` (legacy alias of `vmaf`). |
| `--output PATH` | stdout | Manifest destination. |

The remaining flags (`--encoder`, `--framerate`, `--duration`, `--pix-fmt`,
`--crf-sweep`, `--src-width`, `--src-height`, `--score-backend`, `--vmaf-bin`,
`--neg`, `--workdir`, `--max-concurrent-decodes` and the uncertainty options
`--with-uncertainty`, `--uncertainty-sidecar`, `--rung-overlap-threshold`) are
documented, with defaults, in the
[`ladder` flag table](vmaf-tune-ladder.md#flags).

## See also

- [`vmaf-tune.md`](vmaf-tune.md) — overview of every subcommand.
- [`vmaf-tune-ladder.md`](vmaf-tune-ladder.md) — full reference.
- [`vmaf-tune-ladder-default-sampler.md`](vmaf-tune-ladder-default-sampler.md)
  — default sampler details.
- [ADR-0295](../adr/0295-vmaf-tune-phase-e-bitrate-ladder.md) — Phase E
  design.
- [ADR-0307](../adr/0307-vmaf-tune-ladder-default-sampler.md) — default
  sampler decision.
