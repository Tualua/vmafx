<!-- markdownlint-disable MD013 MD060 -->
# ADR-1565: A libx265 two-pass cell at a CRF is pass 1 at the CRF, then ABR at pass 1's bitrate

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: tools, vmaf-tune, python, go, codec, fork-local

## Context

`vmaf-tune corpus --two-pass` with `libx265` could not encode. The adapter
kept `-crf` in both passes and x265 refuses that on the second:
"Constant rate-factor is incompatible with 2pass without vbv-maxrate in the
previous pass", exit 183 (state row `T-VMAFTUNE-X265-TWO-PASS-CRF-2026-10-04`;
`test_real_x265_two_pass_smoke` showed it under `VMAF_TUNE_INTEGRATION=1`).
`libx264` drops `-crf` in pass mode instead, so its two-pass cells ignore the
CRF they are labelled with. What a two-pass cell at a CRF should mean is a
design question: a bitrate target, a VBV cap, or something else.

Checked with ffmpeg n9.0.2 and x265 on a 2 s 320x180 clip: pass 1 at
`-crf 28` with `-x265-params pass=1:stats=...` encodes and writes a real
bitstream; pass 2 with `-b:v <kbps>k -x265-params pass=2:stats=...` encodes;
pass 2 with `-crf 28` exits 183.

## Decision

A `libx265` two-pass cell at a CRF runs pass 1 at that CRF, writing its
bitstream to a file; `ffprobe` reports the container bit rate of that file; pass
2 is an ABR encode (`-b:v <kbps>k`, no `-crf`) at that bitrate, reading pass
1's stats. The cell records that its rate control is ABR at that bitrate: the
result's request lists `-b:v <kbps>k` in `extra_params`, which the corpus row's
`extra_params` column carries, and the `crf` column stays the pass-1 CRF. If
ffprobe reports no bit rate the cell fails (exit 1, `[pass 1 bitrate
unavailable]`) and pass 2 does not run; there is no fallback to a guessed
bitrate or to single pass.

The behaviour belongs to the adapter flag `two_pass_abr_at_pass1_bitrate`
(Python) / `TwoPassABRAtPass1Bitrate` (Go), set for libx265 only; the adapter
version becomes `2` so cached results never alias the old behaviour. Python
`vmaftune.encode._encode_abr_two_pass` and Go `corpus.runABRTwoPassEncode` are
the two implementations; the argv swap is `_with_abr_rate_control` /
`withABRRateControl`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Pass 1 at the CRF, pass 2 ABR at pass 1's bitrate (chosen) | Keeps the CRF meaning (the bitrate it produces); uses only options x265 accepts; measured on a real encode | The cell is no longer a pure CRF cell; one ffprobe call per cell | Chosen by the maintainer (popup 2026-10-04) |
| Pass 1 with `vbv-maxrate`, pass 2 keeps `-crf` | Keeps `-crf` in both passes | Needs a VBV bitrate to choose, which is the open question again; `crf` plus VBV changes the rate control | A second free parameter with no principled value |
| Drop `-crf` and take the bitrate from the user | Matches libx264's behaviour | The cell ignores its CRF label | The CRF is the corpus axis |
| Fall back to single pass for libx265 | No new code | `--two-pass` silently does nothing for x265 | Silent fallback |
| New corpus columns (rate control, ABR bitrate) | Explicit | A corpus schema bump (v4) and a Go row mirror for one adapter | Disproportionate; `extra_params` already lists the argv the encode used |

## Consequences

- **Positive**: `corpus --two-pass` with `libx265` encodes; the row says how.
- **Negative**: a libx265 two-pass row's bitrate and VMAF belong to an ABR
  encode, not to a CRF encode; trainers that read `crf` as the quality knob
  should filter on `extra_params` containing `-b:v`. ffprobe becomes a
  dependency of a libx265 two-pass run, resolved next to the ffmpeg binary.
- **Neutral / follow-ups**: libx264's two-pass cells still drop `-crf`
  (T-VMAFTUNE-TWOPASS-CRF-INVALID-2026-08-30); aligning them is a separate
  decision.

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): x265 two-pass at a CRF:
  pass 1 at the CRF, then a two-pass ABR encode at the bitrate pass 1 produced;
  the cell records that its rate control is ABR at that bitrate.
- State row `T-VMAFTUNE-X265-TWO-PASS-CRF-2026-10-04`.
- [ADR-0333](0333-vmaf-tune-multi-pass-encoding.md),
  [ADR-0595](0595-codec-adapter-two-pass-real.md).
