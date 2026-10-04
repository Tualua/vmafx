<!-- markdownlint-disable MD013 MD060 -->
# ADR-1540: The mobilesal extractor pads frames to a multiple of 8 for the saliency students

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ai, dnn, tiny-ai, correctness, fork-local

## Context

`saliency_student_v1` and `saliency_student_v2`, the recommended models for
the `mobilesal` extractor, are U-Nets with three stride-2 stages whose decoder
concatenates each upsampled stage with its encoder skip. A side that is not a
multiple of 8 gives the two tensors different sizes, and ONNX Runtime stops
with `Concat ... Axis 2 has mismatched dimensions of 81 and 80` (docs-audit
defect 26): the Netflix 576x324 pair could not be scored at all. Probing the
graphs with ONNX Runtime: 320x576 and 328x576 run, 324x576, 324x580 and
330x578 fail, 8x8 runs and 4x4 fails, for both students. The placeholder
`mobilesal.onnx` (a 1x1 convolution and a sigmoid) runs at any size.

## Decision

We will pad the frame, after the YUV to RGB conversion, to the next multiple
of 8 in each direction by repeating its last column and last row, run the
model on the padded size, and average the saliency map over the frame's own
area. A frame whose sides already are multiples of 8 is fed unchanged, and a
map of another size than the input is an error.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Repeat the last column and row (chosen) | No artificial edge at the border; frames already a multiple of 8 and the 1x1 placeholder give bit-identical scores | The bottom and right of the frame see repeated content | — |
| Pad with zeros in ImageNet-normalised space (as `tools/vmaf-tune/src/vmaftune/saliency.py` does, to a multiple of 32) | Matches that tool | A hard grey border next to the frame, which a saliency model can respond to | An edge the frame does not have |
| Refuse sides that are not multiples of 8 with a clear error | Nothing synthetic enters the model | 576x324 and every other common odd size stays unscorable | The brief allows it, but padding serves the user and leaves exact sizes unchanged |
| Resize to a multiple of 8 | No padding | Changes every pixel and the aspect ratio; the map no longer lines up with the frame | Alters the input more than padding |
| Read a size multiple from the sidecar | Model-specific | The extractor cannot see the session's sidecar; the students would need new metadata | One constant covers the shipped models |

## Consequences

- **Positive**: the students score any 8-bit frame size; measured unchanged on
  1920x1080 (checkerboard pair, `--precision max`) and, for the placeholder,
  on 576x324.
- **Negative**: the padded border slightly changes the map near the bottom
  and right edges of frames that are not multiples of 8.
- **Neutral / follow-ups**: `tools/vmaf-tune`'s `compute_saliency_map()`
  still refuses heights not divisible by 8 before its own padding to 32;
  aligning it is a vmaf-tune change.

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): fix every docs-audit defect
  now; saliency students get frames that are not a multiple of 8 padded or
  refused with a clear error, tested at 576x324.
- Docs-audit defect 26.
- [ADR-0218](0218-mobilesal-saliency-extractor.md),
  [ADR-0444](0444-saliency-student-v2-production-promotion.md).
