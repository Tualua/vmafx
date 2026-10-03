<!-- markdownlint-disable MD060 -->
# `vmaf-perShot` — per-shot CRF predictor sidecar

`vmaf-perShot` turns a single YUV reference into a **per-shot CRF plan**: a
CSV or JSON sidecar your encoder can consume to drive content-adaptive bitrate
without an ML framework on the encode side. It is a fork-added binary
([ADR-0222](../adr/0222-vmaf-per-shot-tool.md)), built as `vmaf-perShot`
(`build/tools/vmaf-perShot`).

It does **not** measure VMAF: its output is an encoder hint, not a quality
score. Use [`vmaf`](cli.md) to verify the post-encode VMAF.

## Pipeline

```text
ref.yuv ──► vmaf-perShot ──► plan.csv ──► encoder (--zones / --crf table)
                                │
                                └──► (post-encode) vmaf to verify VMAF
```

The plan describes shot boundaries and a recommended per-shot CRF
clamped to `[--crf-min, --crf-max]`. Downstream encoders are free to
adjust — every per-shot signal is exposed in the sidecar.

## Quick start

```shell
# Generate a CSV plan targeting VMAF 90, CRF 18-35.
vmaf-perShot \
    --reference ref.yuv \
    --width 1920 --height 1080 \
    --pixel_format 420 --bitdepth 8 \
    --output plan.csv \
    --target-vmaf 90 \
    --crf-min 18 --crf-max 35

# JSON variant (for pipelines that prefer it).
vmaf-perShot \
    --reference ref.yuv \
    --width 1920 --height 1080 \
    --pixel_format 420 --bitdepth 8 \
    --output plan.json \
    --format json
```

## Required flags

| Flag                | Argument | Notes                                          |
|---------------------|----------|------------------------------------------------|
| `-r / --reference`  | PATH     | Planar YUV input.                              |
| `-w / --width`      | N        | Frame width in pixels.                         |
| `-h / --height`     | N        | Frame height in pixels.                        |
| `-p / --pixel_format` | `420 \| 422 \| 444` | Planar YUV chroma subsampling.   |
| `-b / --bitdepth`   | `8 \| 10 \| 12 \| 16` | Planar YUV bit depth.             |
| `-o / --output`     | PATH     | Plan destination (CSV or JSON).                |

## Optional flags

| Flag                  | Default | Notes                                              |
|-----------------------|---------|----------------------------------------------------|
| `-t / --target-vmaf`  | `90`    | Target VMAF; reduces CRF as target rises.          |
| `-m / --crf-min`      | `18`    | Lower CRF clamp.                                   |
| `-M / --crf-max`      | `35`    | Upper CRF clamp.                                   |
| `-d / --diff-threshold` | `12.0` | Shot-detector cutoff (8-bit mean-abs-delta units). |
| `-f / --format`       | `csv`   | `csv` or `json`.                                   |
| `-F / --frames`       | `0`     | Maximum frames to scan (0 = all). Aliases: `--frame_cnt`, `--max-frames`. |
| `--help`              | -       | Print usage and exit `0`.                          |

## Output format — CSV

```text
shot_id,start_frame,end_frame,frames,mean_complexity,mean_motion,predicted_crf
0,0,3,4,0.000051,0.020046,25.48
1,4,47,44,0.019353,0.016716,24.62
```

Columns:

- **`shot_id`** — zero-based ordinal.
- **`start_frame` / `end_frame`** — inclusive frame range.
- **`frames`** — `end_frame - start_frame + 1`.
- **`mean_complexity`** — mean luma sample variance over the shot,
  normalised to `[0, 1]` per pixel (then squared into variance units).
- **`mean_motion`** — mean absolute frame-to-frame luma delta over
  the shot, normalised to `[0, 1]` per pixel.
- **`predicted_crf`** — the v1 linear-blend prediction, clamped into
  `[--crf-min, --crf-max]`.

## Output format — JSON

```json
{
  "target_vmaf": 90.00,
  "crf_min": 18,
  "crf_max": 35,
  "shots": [
    {"shot_id": 0, "start_frame": 0, "end_frame": 3, "frames": 4,
     "mean_complexity": 0.000051, "mean_motion": 0.020046,
     "predicted_crf": 25.48}
  ]
}
```

## How the CRF prediction works (v1)

The v1 predictor is a transparent linear blend, not a trained model.
This keeps the binary debuggable today; v2 (separate ADR) will wire
a small MLP once a labelled per-shot CRF corpus exists. See
[ADR-0222](../adr/0222-vmaf-per-shot-tool.md) §Decision for the
exact formula.

The intuitions baked in:

- Higher target VMAF → lower CRF (better quality).
- Higher complexity (busier shot) → lower CRF (artefacts more visible).
- Higher motion → higher CRF (motion masks artefacts; saves bits).
- Very short shots (< 24 frames) → smaller motion bonus (rate-control
  startup amortisation).

## Shot detector (v1 fallback)

The built-in detector is a frame-difference heuristic: a frame's mean
absolute luma delta vs. its predecessor is compared against
`--diff-threshold` (8-bit domain, default `12.0`). Detected cuts only
fire after the running shot has reached 4 frames (suppresses flash /
fade flicker).

This is intentionally simple. The TransNet V2 extractor
([ADR-0223](../adr/0223-transnet-v2-shot-detector.md)) is registered in libvmaf,
but `vmaf-perShot` does not use it yet: there is no `--shots PATH` option for a
pre-computed shot map, and the frame-difference detector always runs.

## Worked example: feeding x265 `--zones`

1. Build a plan:

    ```shell
    vmaf-perShot \
        --reference ref.yuv -w 1920 -h 1080 -p 420 -b 8 \
        --output plan.csv
    ```

2. Convert `plan.csv` to an x265 `zones=` value (one `start,end,crf=Q` zone per
   row, joined by `/`):

    ```shell
    awk -F, 'NR>1 { printf("%s,%s,crf=%.0f/", $2, $3, $7) }' plan.csv \
        | sed 's:/$::' > zones.txt
    ```

    For the plan above, `zones.txt` holds `0,3,crf=25/4,47,crf=25`.

3. Encode with the zone string. `-x265-params` needs the `zones=` key:

    ```shell
    ffmpeg -i ref.y4m -c:v libx265 \
        -x265-params "zones=$(< zones.txt)" out.mp4
    ```

4. Decode the result to `.y4m` and verify the post-encode VMAF. `vmaf` reads
   only `.y4m` and `.yuv`:

    ```shell
    ffmpeg -i out.mp4 -pix_fmt yuv420p out.y4m
    vmaf --reference ref.y4m --distorted out.y4m --output verify.json
    ```

## Input Formats

`vmaf-perShot` reads planar YUV directly with no demuxer. The shot
detector and CRF prior use only the luma plane, but the scanner still
consumes every chroma byte according to `--pixel_format` so frame
iteration stays aligned for 4:2:0, 4:2:2, and 4:4:4 inputs. High-bit-
depth inputs use little-endian 16-bit sample containers for 10/12/16-bit
content, matching the fork's raw-YUV CLI convention.

EOF is clean only before the first luma byte of a new frame. A trailing
luma-only frame, partial chroma plane, or underlying read error exits non-zero
with an `incomplete raw YUV frame` or `read error` diagnostic; no partial frame
is included in the emitted plan.

## Limitations (v1)

- Frame-difference detector misses dissolves / cross-fades — those
  segments collapse into a single longer shot. The TransNet V2 extractor
  would fix this once the tool can consume a shot map.
- The linear-blend CRF is a static prior, not a trained fit. v2
  ships a small MLP under the same CSV / JSON schema (no consumer
  break expected).
- Shot table capped at 4096 entries (covers ≈3-hour content at
  one cut every 2 s); overflow surfaces as `ENOSPC`.
- **Frame limit.** `-F / --frames <N>` (aliases `--frame_cnt`, `--max-frames`)
  stops the scan after `N` frames. The tool exits 0 and emits the plan for the
  scanned prefix.
- **Unbounded scan.** With `--frames` omitted or `0`, the scan accepts up to
  exactly `UINT32_MAX` (4294967295) complete frames. At that boundary the reader
  probes once more: EOF accepts the input, while one more complete frame
  reports `vmaf-perShot: input exceeds the 4294967295-frame scan limit`
  (`EFBIG`) before the frame is indexed or recorded.
- **Endless sources.** For a FIFO with a live writer or `/dev/zero`, pass
  `--frames <N>` to guarantee bounded execution.

The boundary check fixes the off-by-one of
[ADR-1287](../adr/1287-cli-tool-unbounded-loop-ceilings.md) and closes
`T-PER-SHOT-ENDLESS-INPUT-NOT-A-TIMEOUT-2026-09-21` in [state.md](../state.md).

## Related

- [ADR-0222](../adr/0222-vmaf-per-shot-tool.md) — design + alternatives.
- [ADR-0223](../adr/0223-transnet-v2-shot-detector.md) — TransNet V2
  extractor (registered; not yet an input of this tool).
- [`cli.md`](cli.md) — the main `vmaf` scoring CLI.
- [`docs/ai/roadmap.md`](../ai/roadmap.md) §2.4 — the broader per-shot
  CRF roadmap.
