<!-- markdownlint-disable MD060 -->
# `vmaf-tune --two-pass`: multi-pass encoding

Add `--two-pass` to `vmaf-tune corpus` or `vmaf-tune recommend` to run
each encode twice: pass 1 analyses the source and writes a stats file,
pass 2 reads it to allocate bits better. The default stays single-pass.
Only some adapters can do a true two-invocation 2-pass; the codec matrix
below says which, and what the others offer instead
([ADR-0333](../adr/0333-vmaf-tune-multi-pass-encoding.md),
[ADR-0546](../adr/0546-audit-bundle-vulkan-saliency-modelcard.md)).

## Quick start

```shell
vmaf-tune corpus \
    --source ref.yuv --width 1920 --height 1080 \
    --pix-fmt yuv420p --framerate 24 --duration 5 \
    --encoder libx265 --preset medium --crf 23 \
    --two-pass \
    --output corpus_2pass.jsonl
```

The driver creates a per-encode stats file in a fresh temporary
directory (`vmaftune-2pass-XXXXXX` under `tempfile.gettempdir()`), runs
both passes back to back, and removes the stats file and the known
encoder sidecars when the run ends, whether it succeeded or not.

| Flag | Subcommands | Default | Meaning |
|---|---|---|---|
| `--two-pass` | `corpus`, `recommend` | off | Run a 2-pass encode for adapters that support it. |

## When it helps

Two-pass pays off most in target-bitrate workflows, such as VOD ladder
generation and codec comparisons at a fixed bitrate. A constant-quality
(CRF) encode already adapts its QPs frame by frame from the encoder's
look-ahead, so the gain at a fixed CRF is smaller. Typical figures, not a
contract:

- About +1 to +3 VMAF points at a fixed bitrate target for `libx265`
  against 1-pass ABR, for typical content (see the x265 rate-control
  documentation).
- About twice the encode wall time, because the second pass roughly
  doubles the cost. `encode_time_ms` in the corpus row is the sum of both
  passes.

## Codec support matrix

ADR-0546 closed the contract for every adapter. An adapter either runs a
real two-invocation 2-pass, returns single-invocation quality-boost flags
you can splice into `extra_params`, or raises a typed error saying why it
cannot.

| Codec | `supports_two_pass` | `two_pass_args(1, p)` returns | Notes |
|---|---|---|---|
| `libx264` | yes | `-pass 1 -passlogfile <prefix>` | FFmpeg-native two-invocation 2-pass. `-crf` is omitted in a 2-pass encode. |
| `libx265` | yes | `-x265-params pass=1:stats=<path>` | x265 routes pass control through its codec-private payload. |
| `libvpx-vp9` | yes | `-pass 1 -passlogfile <prefix>` | FFmpeg-native 2-pass; CRF mode pinned with `-b:v 0`. |
| `libaom-av1` | yes | `-pass 1 -passlogfile <prefix>` | FFmpeg-native 2-pass. |
| `libvvenc` | yes | `-pass 1 -passlogfile <prefix>` | FFmpeg 6.1 or newer translates `-pass` to VVenC's `RcStatsFile`. |
| `libsvtav1` | no | `-pass 1 -passlogfile <prefix>` | SVT-AV1 forbids multi-pass in CRF mode. See below. |
| `h264_nvenc`, `hevc_nvenc`, `av1_nvenc` | no | `-multipass fullres` | In-encoder full-resolution analysis in one invocation. Needs NVENC VBR rate control and a target bitrate. |
| `h264_qsv`, `hevc_qsv`, `av1_qsv` | no | `-extbrc 1 -look_ahead_depth 40` | Extended-BRC look-ahead window inside one invocation. |
| `h264_amf`, `hevc_amf`, `av1_amf` | no | `-preanalysis true` | AMF pre-analysis stage inside one invocation. |
| `h264_videotoolbox`, `hevc_videotoolbox`, `av1_videotoolbox`, `prores_videotoolbox` | no | raises `VideoToolboxTwoPassUnsupportedError` | Apple's `VTCompressionSession` has no multi-pass API. |

Notes on the "no" rows:

- **`libsvtav1`.** SVT-AV1 refuses multi-pass in CRF mode (checked
  against v4.1.0: `Svt[error]: CRF does not support multi-pass. Use
  single pass.`). The adapter pins CRF, so it declares no 2-pass support.
  It still returns VBR-mode argv for callers who switch to a
  bitrate-targeted mode through `extra_params`.
- **NVENC, QSV, AMF.** For hardware encoders pass 1 returns the
  look-ahead flags and pass 2 returns nothing, because the analysis
  already ran inside the single invocation. Splice the pass-1 value into
  `EncodeRequest.extra_params` for a quality-boosted single-pass encode.
- **VideoToolbox.** For true 2-pass on macOS, switch to `libx264`,
  `libx265`, `libsvtav1`, `libaom-av1` or `libvvenc`; all ship in the
  same FFmpeg build. Calling `adapter.two_pass_args()` always raises the
  typed error, so a caller can tell an API limitation from a missing
  implementation.

## Unsupported encoders

With `--two-pass` and an encoder whose `supports_two_pass` is false,
`vmaf-tune` writes a one-line warning to stderr and runs single-pass:

```text
vmaf-tune: encoder 'libsvtav1' does not support 2-pass encoding; falling back to single-pass.
```

The Python API can fail loudly instead: pass `on_unsupported="raise"` to
`run_two_pass_encode`, which raises `ValueError`. If pass 1 fails, pass 2
does not run, and the result's `stderr_tail` starts with `[pass 1
failed]`.

## Hardware quality-boost composition

To use a hardware encoder's single-invocation boost with the normal
single-pass driver, take `adapter.two_pass_args(1, ...)` and put it in
`EncodeRequest.extra_params`:

```python
from pathlib import Path

from vmaftune.codec_adapters import get_adapter
from vmaftune.encode import EncodeRequest, run_encode

adapter = get_adapter("h264_nvenc")
boost = adapter.two_pass_args(1, Path("/tmp/_unused"))  # ('-multipass', 'fullres')

req = EncodeRequest(
    source=ref, width=1920, height=1080, pix_fmt="yuv420p", framerate=24.0,
    encoder="h264_nvenc", preset="slow", crf=22, output=out,
    extra_params=boost,  # quality-boosted single-pass
)
run_encode(req)
```

## Interaction with other features

- **Sample-clip mode.** Both passes use the same `-ss <start> -t <N>`
  input slice, and the stats file is unique per slice, so
  [`--sample-clip-seconds`](vmaf-tune-hdr-and-sampling.md#clip-sampling)
  composes with `--two-pass` with no extra handling.
- **Cache.** The content-addressed encode cache
  ([`vmaf-tune-cache.md`](vmaf-tune-cache.md)) is a Python-API feature,
  off by default (`CorpusOptions(cache_enabled=True, cache_dir=...)`),
  with no CLI flag. Its key holds the source hash, encoder, preset, CRF,
  adapter version and FFmpeg version. It does not hold the pass count, so
  with the cache enabled a cached 1-pass result can answer a 2-pass
  request for the same cell.
- **Encoder statistics.** For `libvpx-vp9`, FFmpeg's pass log is binary
  first-pass data, not the x264 and x265 text schema that
  `encoder_stats.py` reads, so the per-frame statistics columns stay
  zero.

## See also

- [`vmaf-tune.md`](vmaf-tune.md): the base tool.
- [`vmaf-tune-codec-adapters.md`](vmaf-tune-codec-adapters.md): the
  adapter registry and the `two_pass_args` contract.
- [`vmaf-tune-codec-hardware.md`](vmaf-tune-codec-hardware.md): the
  hardware families.
- [`vmaf-tune-recommend.md`](vmaf-tune-recommend.md): the `recommend`
  subcommand that also takes `--two-pass`.
- [`vmaf-tune-cache.md`](vmaf-tune-cache.md): the encode cache.
- [ADR-0333](../adr/0333-vmaf-tune-multi-pass-encoding.md): the Phase F
  design.
