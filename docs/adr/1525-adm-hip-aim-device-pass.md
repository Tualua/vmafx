<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1525: adm_hip computes AIM on the device and is dispatched

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: Lusoris
- **Tags**: hip, gpu, adm, bit-exactness, fork-local

## Context

`adm_hip` had no AIM contrast-measure pass, so it could not emit
`VMAF_integer_feature_aim_score` and `VMAF_integer_feature_adm3_score`, the
two features the default model `vmaf_v1.0.16_3d0h` reads, and it carried no
`VMAF_FEATURE_EXTRACTOR_HIP` flag (`.flags = 0`). Under `--backend hip` the
model's ADM therefore ran on the CPU `integer_adm` extractor while the rest of
the model ran on the device, and `--feature adm` never selected the twin. The
documentation audit recorded the missing flag as defect 16; the missing pass
is `T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05`, whose SYCL half
ADR-1362 closed. RC3 requires every twin to return the CPU extractor's bits.

The CUDA twin computes AIM with dedicated kernels (ADR-0746) that recompute
the CSF of the restored part r at all nine taps of every threshold; the SYCL
twin stores |csf(r)| / 30 beside the DLM band in its CSF pass (ADR-1362). The
HIP twin is a call-graph port of the CUDA twin, and the CUDA kernels are
bit-identical to the CPU (`adm.cuda`, ADR-1416).

## Decision

`adm_hip` gets the CUDA twin's AIM kernels, ported to HIP:
`adm_cm_aim_line_kernel_4` (scale 0) and `i4_adm_cm_aim_line_kernel`
(scales 1 to 3) in `integer_adm/adm_cm.hip`, reducing each row in shared
memory and folding it once, as the DLM kernels do. Every rounding shift comes
from the CPU's `adm_cm_ctx_init()` / `i4_adm_cm_ctx_init()` on the host (the
scale-0 DLM launch now takes its shifts there too), the AIM accumulators are
the third block of the frame's result buffer (cleared and read back with the
others), and the host concludes each scale with `adm_cm_result()` /
`i4_adm_cm_result()` at noise weight 0 and forms `aim` and `adm3` with
`vmaf_adm_scale_ratios()` and `vmaf_adm3_score_named()`, as
`integer_adm.c` does. `adm_skip_aim` joins the option table. With every output
bit-identical, the twin claims `aim` / `adm3` and carries the HIP flag, so
`--backend hip` and the default model select it. Every signed right shift of
`adm_cm.hip` goes through one arithmetic-shift helper written on unsigned
bits, which clears the file's `bugprone-signed-bitwise` findings without
changing a value.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Port the CUDA ADR-0746 kernels (chosen) | Proven bit-identical on CUDA; no new buffer, no change to the CSF pass; the HIP twin stays a call-graph port of the CUDA one | Recomputes csf(r) 9 times per threshold tap set: on the gfx1036 AIM costs twice the DLM pass | — |
| Port the SYCL ADR-1362 design (store \|csf(r)\| / 30 in the CSF pass) | AIM costs about as much as the DLM pass | Changes the CSF kernel and the buffer layout of a twin already declared exact; diverges from the CUDA call graph the HIP twin follows | Kept as the RC8 tuning step (`T-HIP-ADM-AIM-INLINE-COST-2026-10-04`): speed is RC8's, exactness RC3's |
| Keep the twin unflagged and AIM on the CPU | No cost on a weak iGPU | `--backend hip` keeps running the default model's ADM on the CPU, the defect this ADR closes | The brief and RC3 ask for the device twin |
| Flag the twin without AIM | One-line change | The model's `aim` / `adm3` would still come from the CPU extractor, which recomputes the whole DWT: two ADM extractors per frame | Wastes work and keeps the defect for the default model |

## Consequences

- **Positive**: under `--backend hip` every feature of the default model
  runs on the device and its VMAF equals `--backend cpu` on every frame
  measured (Netflix pair, 1080p checkerboards, 50 frames of BBB 3840x2160);
  `adm_hip` returns 4141 of 4141 values identical to the CPU, `aim` and
  `adm3` included.
- **Negative**: on the 2-unit gfx1036 the twin is slower than the 16-thread
  CPU extractor (218 against 14.5 ms per 3840x2160 frame), and the default
  model under `--backend hip` goes from 65 to 273 ms per 3840x2160 frame.
  Discrete AMD GPUs are not measured.
- **Neutral / follow-ups**: `T-HIP-ADM-AIM-INLINE-COST-2026-10-04` (RC8)
  carries the SYCL design as the tuning step; the parity gate's `adm` cell
  still lists the DLM outputs only (`aim` / `adm3` are covered by
  `test_hip_adm_exact`).

## References

- `.workingdir/evidence/docs-audit-2026-10-03/code-defects.md` item 16
  (local, not public).
- [ADR-0746](0746-cuda-integer-adm3-aim-parity.md) (CUDA AIM kernels),
  [ADR-1362](1362-sycl-integer-adm-aim-device-pass.md) (SYCL AIM pass),
  [ADR-1423](1423-hip-adm-cpu-row-rounding.md) (`adm_hip` exact),
  [ADR-0530](0530-hip-feature-flag-promotion-and-picture-buffer.md) (per-extractor flag promotion).
- Source: maintainer decision of 2026-10-04, "Track all, fix now".
