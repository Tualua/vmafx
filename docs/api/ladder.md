# `vmaftune.ladder`: Python API reference

Use `vmaftune.ladder` to turn a cloud of encoded (resolution, bitrate, VMAF)
observations into a per-title ABR ladder: compute the Pareto hull, pick a
few rungs along it, and write an HLS, DASH or JSON manifest. The
`vmaf-tune ladder` CLI subcommand uses it, and you can call it directly from
a custom pipeline. The source is
[`tools/vmaf-tune/src/vmaftune/ladder.py`](../../tools/vmaf-tune/src/vmaftune/ladder.py).

For the CLI see [vmaf-tune ladder](../usage/vmaf-tune-ladder.md); for the
grid-based variant see
[vmaf-tune bitrate ladder](../usage/vmaf-tune-bitrate-ladder.md).

## Quick start

This program needs no encoder: it works from observations you already have.
The `convex_hull` / `select_knees` / `emit_manifest` trio does not import the
corpus or codec-adapter modules.

```python
from vmaftune.ladder import LadderPoint, convex_hull, emit_manifest, select_knees

raw = [
    LadderPoint(640, 360, 400, 62.0, 36),      # width, height, kbps, vmaf, crf
    LadderPoint(640, 360, 800, 74.0, 30),
    LadderPoint(1280, 720, 1500, 84.0, 32),
    LadderPoint(1280, 720, 2500, 90.0, 27),
    LadderPoint(1920, 1080, 4000, 94.0, 29),
    LadderPoint(1920, 1080, 6000, 96.5, 24),
]
hull = convex_hull(raw)
rungs = select_knees(hull, n=4, spacing="log_bitrate")
print(emit_manifest(rungs, format="hls"))
```

The output is an HLS master playlist with four variants, from
`rendition_640x360_400k.m3u8` to `rendition_1920x1080_6000k.m3u8`.

To sample real encodes instead, use `build_ladder()` with
`make_default_sampler()` (see [Sampling](#sampling)) or the one-call
`build_and_emit()`.

## Data types

All are frozen dataclasses (immutable, hashable).

| Type | Fields | Role |
| --- | --- | --- |
| `LadderPoint` | `width`, `height`, `bitrate_kbps`, `vmaf`, `crf`; property `pixel_count` | One sampled observation. `crf` is the encoder quality value the sampler converged on, reusable by later encodes. |
| `Rendition` | `width`, `height`, `bitrate_kbps`, `vmaf`, `crf` | One rung of the final ladder. Returned by `select_knees`, consumed by `emit_manifest`. |
| `Ladder` | `src: Path`, `encoder: str`, `points: tuple[LadderPoint, ...]` | The raw sampled grid returned by `build_ladder`. |
| `UncertaintyLadderPoint` | the `LadderPoint` fields plus `vmaf_low`, `vmaf_high`; property `interval_width`; method `as_ladder_point()` | A point with a conformal prediction interval ([ADR-0393](../adr/0393-fr-regressor-v2-probabilistic.md)). |

`UncertaintyLadderPoint` is a standalone dataclass, not a subclass of
`LadderPoint`, so `isinstance(p, LadderPoint)` is false for it. Convert with
`as_ladder_point()` before `convex_hull()` or `select_knees()`.
`interval_width` is `max(0, vmaf_high - vmaf_low)`: small means a tight
prediction, large a wide one.

## Pipeline functions

### `convex_hull`

```python
def convex_hull(points: Iterable[LadderPoint]) -> list[LadderPoint]
```

Returns the upper convex hull of the (bitrate, VMAF) cloud, in two steps:

1. A Pareto filter drops every point that another point matches or beats on
   both axes (lower or equal bitrate with higher or equal VMAF, one strictly).
2. The remaining rising staircase is cut down to its concave envelope, so
   an ABR client switching between hull points sees diminishing returns.

The result is sorted by ascending bitrate with strictly increasing bitrate
and VMAF; an empty input returns `[]`. Pass the hull, not raw observations,
to `select_knees`.

### `select_knees`

```python
def select_knees(
    hull: Sequence[LadderPoint], n: int = 5, *, spacing: str = "log_bitrate"
) -> list[Rendition]
```

Picks up to `n` rungs from the hull. `spacing` is keyword-only.

| `spacing` | Rungs are evenly spaced in | Notes |
| --- | --- | --- |
| `"log_bitrate"` (default) | `log(bitrate)` | Apple HLS authoring convention: each rung roughly doubles the bitrate. |
| `"vmaf"` | VMAF | Equal VMAF gap per rung. |
| `"uniform"` | VMAF | Legacy alias of `"vmaf"`. It does not mean a linear bitrate range. |

Each ideal coordinate snaps to the nearest hull point. The first and last
hull points are always included. The result is sorted by ascending bitrate.

Edge cases:

- An empty hull returns `[]`.
- `n <= 0` raises `ValueError`.
- `n == 1` returns the single highest-VMAF hull point.
- A hull with `n` or fewer points is returned whole, as renditions.
- An unknown `spacing` raises `ValueError`, checked only when selection
  actually needs it (a hull longer than `n`).

### `emit_manifest`

```python
def emit_manifest(
    ladder: Sequence[Rendition], format: str = "hls", *,
    samples: Sequence[LadderPoint] | None = None,
) -> str
```

Serialises renditions, sorted by ascending bitrate, to a string. An unknown
`format` raises `ValueError`.

| `format` | Output |
| --- | --- |
| `"hls"` (default) | `#EXTM3U` and `#EXT-X-VERSION:6`, then one `#EXT-X-STREAM-INF` per rung with `BANDWIDTH` in bits per second, `RESOLUTION=WxH` and a fixed `CODECS="avc1.640028"`. Variant URIs are placeholders `rendition_<W>x<H>_<kbps>k.m3u8`; point them at your real playlists. There is no `#EXT-X-TARGETDURATION`. |
| `"dash"` | MPD with one `AdaptationSet` and one `Representation` per rung, with `BaseURL` placeholders `rendition_<W>x<H>_<kbps>k.mp4`. Minimal. |
| `"json"` | The machine-readable form, below. |

The JSON document is an object:

```json
{
  "schema": "vmaf-tune-ladder/v1",
  "renditions": [
    {"width": 640, "height": 360, "bitrate_kbps": 400, "bandwidth_bps": 400000,
     "vmaf": 62.0, "crf": 36}
  ],
  "samples": []
}
```

`samples` is always present. It carries the pre-hull cloud (same fields per
entry) when you pass `samples=`, which `vmaf-tune report` reads to draw the
hull overlay. The HLS and DASH formats ignore `samples`.

## Sampling

These functions produce the observations.

```python
def build_ladder(src, encoder, resolutions, target_vmafs, *, sampler=None) -> Ladder
def make_default_sampler(*, pix_fmt="yuv420p", framerate=24.0, duration_s=1.0,
                         crf_sweep=None, src_width=None, src_height=None,
                         cloud_sink=None, score_backend=None,
                         vmaf_model=DEFAULT_MODEL) -> SamplerFn
```

- `build_ladder()` calls `sampler(src, encoder, width, height, target_vmaf)`
  for every (resolution, target VMAF) cell and returns a `Ladder`. With
  `sampler=None` it runs the default sampler, which encodes a CRF sweep
  through the vmaf-tune corpus loop and picks the row closest to the target
  ([ADR-0307](../adr/0307-vmaf-tune-ladder-default-sampler.md)).
  `SamplerFn` is `Callable[[Path, str, int, int, float], LadderPoint]`.
  Inject your own to avoid live encodes, for example in tests.
- `make_default_sampler()` returns a sampler bound to the real source shape.
  Use it instead of the bare default, whose placeholders (24 fps, 1 s,
  `yuv420p`) give wrong bitrate maths on real content. `src_width` and
  `src_height` carry the source resolution when it differs from the rung
  (an ffmpeg `scale` filter is added); `cloud_sink` collects every scored
  CRF row so the JSON `samples` array holds the full cloud;
  `score_backend` and `vmaf_model` are passed to the scoring step.
- `DEFAULT_SAMPLER_CRF_SWEEP = (20, 25, 30, 35, 40)` is the default sweep. It
  starts at 20 so it is valid for every shipped codec adapter (libsvtav1's
  lowest CRF is 20).

### `build_and_emit`

```python
def build_and_emit(src, encoder, resolutions, target_vmafs, *,
                   quality_tiers=5, format="hls", spacing="log_bitrate",
                   sampler=None, with_uncertainty=False,
                   uncertainty_thresholds=None, rung_overlap_threshold=None,
                   point_interval_width=None, extra_samples=None) -> str
```

Runs sample, hull, optional uncertainty recipe, `select_knees` and
`emit_manifest` in one call and returns the manifest string.
`extra_samples` replaces the per-cell picks as the source of the JSON
`samples` array; samples are de-duplicated by `(width, height, crf)`.

## Uncertainty-aware recipe

When points carry conformal intervals, two transforms refine the rung set.
Both change which rungs the ladder evaluates; neither widens the production
flip gate. Background: [conformal VQA](../ai/conformal-vqa.md).

```python
def apply_uncertainty_recipe(
    rungs: Sequence[UncertaintyLadderPoint], *,
    thresholds: ConfidenceThresholds | None = None,
    overlap_threshold: float = DEFAULT_RUNG_OVERLAP_THRESHOLD,   # 0.5
) -> list[UncertaintyLadderPoint]
```

`apply_uncertainty_recipe()` composes the two public steps, in this order, so
the inserted rungs are not pruned again against their parents:

1. `prune_redundant_rungs_by_uncertainty(rungs, *, overlap_threshold=0.5)`:
   walks the rungs by ascending bitrate and drops the lower-bitrate rung of
   an adjacent pair whose intervals overlap by more than `overlap_threshold`
   of the wider interval. The first and last rungs are always kept. Raises
   `ValueError` when `overlap_threshold` is outside `[0, 1]`; inputs of two
   or fewer rungs are returned unchanged.
2. `insert_extra_rungs_in_high_uncertainty_regions(rungs, *, thresholds=None)`:
   for each adjacent pair whose averaged interval width is in the `WIDE`
   band, inserts a synthetic rung at the geometric-mean bitrate and the
   arithmetic-mean VMAF, with the union of the parent intervals and the
   rounded mean CRF. No-op below two rungs.

`ConfidenceThresholds` (`vmaftune.uncertainty`) has two fields; the defaults
are the Research-0067 floor:

| Field | Default (VMAF) | Meaning |
| --- | --- | --- |
| `tight_interval_max_width` | `2.0` | At or below this, the predictor is confident. |
| `wide_interval_min_width` | `5.0` | At or above this, the interval is wide and a rung may be inserted. |

Both must be positive, with tight at most wide. `thresholds=None` means
`ConfidenceThresholds()`.

!!! note
    The recipe does not re-run `convex_hull`. `build_and_emit()` converts the
    result back with `as_ladder_point()` and goes straight to `select_knees`.
    If you call the transforms yourself and need the Pareto property over the
    augmented rungs, call `convex_hull()` on the plain points first.

A worked example is in [vmaf-tune ladder](../usage/vmaf-tune-ladder.md).

## See also

- [vmaf-tune ladder](../usage/vmaf-tune-ladder.md): CLI flags and the
  uncertainty-aware extension.
- [vmaf-tune recommend](../usage/vmaf-tune-recommend.md): per-clip CRF search.
- [Conformal VQA](../ai/conformal-vqa.md): the intervals behind `vmaf_low`
  and `vmaf_high`.
- [vmaf-tune overview](../usage/vmaf-tune.md).
