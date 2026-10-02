---
paths:
  - core/tools/vmaf_roi.c
  - core/tools/vmaf_roi_core.h
  - core/tools/vmaf_roi_input.h
invariant: vmaf_roi sidecar emits x265 or svt-av1 QP offsets with +-12 clamp; per-CTU reduction is mean.
---
# vmaf_roi sidecar contract and encoder format

- **`vmaf_roi` sidecar contract** (T6-2b / ADR-0247) is
  **rebase-sensitive** — encoder drivers depend on exact byte
  layouts:
  - `--encoder x265` emits ASCII per-row grid with two `#`-prefixed
    header lines (`# vmaf-roi qpfile (x265, --qpfile-style)` then
    `# frame=N ctu=S cols=C rows=R strength=F.FFF`), space-separated
    signed integers, one row per CTU row, `\n` terminator.
  - `--encoder svt-av1` emits exactly `cols * rows` bytes of `int8_t`,
    row-major, **no header**.
  - QP-offset clamp is `+-12` (`VMAF_ROI_CORE_QP_OFFSET_MAX`).
  - Reduction is per-CTU **mean** (not max — see ADR-0247 alternatives).
  - Pure helpers (`vmaf_roi_reduce_per_ctu`, `vmaf_roi_saliency_to_qp`)
    live in `vmaf_roi_core.h` so smoke test compiles them
    without dragging libvmaf's link surface in. **Never** move them
    into `.c` TU without revisiting test wiring.
  - Placeholder saliency map (when `--saliency-model` is absent)
    is for smoke-test plumbing only, explicitly documented as
    not-for-real-encodes in `docs/usage/vmaf-roi.md`.
  - `--bitdepth 8|10|12|16` is part of input contract. High-bit-depth
    planar YUV uses little-endian 16-bit containers; frame seeking must
    count chroma planes and sample width even though only luma enters
    saliency path. DNN-facing tensor remains luma8.
  - Private input helpers live in `vmaf_roi_input.h`, shared directly with
    `test_vmaf_roi_bounds`. Keep local depth/extent guards before shifts,
    allocation and reads; rounded high-bit-depth samples saturate at 255
    before `uint8_t` cast. `VMAF_ROI_MAX_DIM` remains existing
    16384 CLI limit. Placeholder traversal validates and uses caller's
    allocation count while preserving radial coordinate arithmetic.
    See [ROI boundary evidence](../../../docs/research/roi-reader-bounds-2026-09-08.md).

- [ADR-0247](../../../docs/adr/0247-vmaf-roi-tool.md) — `vmaf-roi`
  sidecar (per-CTU QP offsets for x265 / SVT-AV1). Encoder format
  contract + per-CTU-mean reduction are rebase-sensitive.
