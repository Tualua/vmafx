<!-- markdownlint-disable MD013 MD060 -->
# `vmaf-tune` ladder default sampler

The default sampler is the production fallback used when
`vmaftune.ladder.build_ladder(..., sampler=None)` runs, and the sampler the
`vmaf-tune ladder` CLI binds. It lives in
`tools/vmaf-tune/src/vmaftune/ladder.py` (`_default_sampler`, bound with the
source shape by `make_default_sampler`).

## Contract

For each `(source, encoder, width, height, target_vmaf)` cell the sampler:

1. Chooses the codec adapter's `medium` preset when it offers one, otherwise
   the middle declared preset.
2. Runs the 5-point CRF sweep `20, 25, 30, 35, 40` through the normal
   `vmaftune.corpus.iter_rows()` encode-and-score path. `--crf-sweep CSV`
   replaces the list on the CLI.
3. Keeps the rows whose encode and score succeeded and picks one with
   `vmaftune.recommend.pick_target_vmaf()`: the lowest-bitrate row whose VMAF
   meets the target (ties go to the higher VMAF, then the lower CRF), or the
   highest-VMAF row when none does.
4. Returns a `LadderPoint(width, height, bitrate_kbps, vmaf, crf)`.

The sampler stays replaceable. Operators who need a different CRF grid, source
shape, sample-clip policy or a precomputed corpus pass an explicit `sampler=`
callable to `build_ladder()`.

## Why the sweep starts at CRF 20

Every CRF in the sweep must lie inside each adapter's `quality_range`, because
`corpus.iter_rows` validates it. The earlier sweep `18,23,28,33,38` started
below the `libsvtav1` lower bound (`quality_range = (20, 50)`), so the ladder
exited with code 2 before encoding anything. `libsvtav1` has the highest lower
bound
of the CRF-style adapters, and CRF 20 meets it, so the default is valid for
them without a `--crf-sweep` override. The spacing of 5 is unchanged. Adapters
with a different quality scale (for example `prores_videotoolbox`, 0..5) still
need an explicit `--crf-sweep`.

## Defaults

| Setting | Value | CLI flag |
| --- | --- | --- |
| CRF sweep | `20,25,30,35,40` | `--crf-sweep` |
| Pixel format | `yuv420p` | `--pix-fmt` |
| Framerate | `24.0` | `--framerate` |
| Nominal duration | `1.0` second | `--duration` |
| Encode cleanup | temporary directory, deleted after each cell | none |

The CLI flags are documented in
[`vmaf-tune-ladder.md`](vmaf-tune-ladder.md#flags).
Calling `_default_sampler` or `build_ladder()` directly keeps the same values
unless you bind others with `make_default_sampler()`.

## Example override

```python
from pathlib import Path
from vmaftune.ladder import LadderPoint, build_ladder

def sampler(src: Path, encoder: str, width: int, height: int, target: float) -> LadderPoint:
    return LadderPoint(width, height, bitrate_kbps=2400.0, vmaf=target, crf=23)

ladder = build_ladder(
    Path("ref.yuv"),
    "libx264",
    resolutions=[(1920, 1080), (1280, 720)],
    target_vmafs=[95.0, 92.0, 88.0],
    sampler=sampler,
)
```

## See also

- [`vmaf-tune.md`](vmaf-tune.md) — overview of every subcommand.
- [`vmaf-tune-ladder.md`](vmaf-tune-ladder.md) — full ladder reference and
  flag table.
- [`vmaf-tune-bitrate-ladder.md`](vmaf-tune-bitrate-ladder.md) — CLI ladder
  workflow in short.
- [ADR-0307](../adr/0307-vmaf-tune-ladder-default-sampler.md) — sampler
  decision.
- [ADR-0295](../adr/0295-vmaf-tune-phase-e-bitrate-ladder.md) — ladder design.
