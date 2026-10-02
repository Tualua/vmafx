<!-- markdownlint-disable MD013 MD060 -->
# Research-1595: SYCL zero-copy feature correctness

- **Status**: Active (Stage 1 done; stages 2 and 3 open)
- **Workstream**: [ADR-1595](../adr/1595-sycl-zerocopy-fail-loud-twin-routing.md), [ADR-1462](../adr/1462-speed-cpu-correctly-rounded-log2.md), [ADR-1596](../adr/1596-sycl-va-import-immediate-cmdlist.md), phase 12
- **Last updated**: 2026-10-02

## Question

On an Intel Arc A380, FFmpeg's `libvmaf_sycl` filter on QSV-decoded (zero-copy)
frames scored the model features correctly but produced nothing for
`feature=name=psnr` or `name=cambi`, and no error. Which layers drop work, and
what has to hold before zero-copy output can be trusted?

## Root cause: three layers

1. **Dispatch.** `read_pictures_sycl_extractors` (`core/src/libvmaf.c`) ran only
   extractors flagged `VMAF_FEATURE_EXTRACTOR_SYCL` and `continue`d past the
   rest, so a CPU extractor registered by `feature=` was skipped silently.
2. **Extractors.** Registered SYCL extractors received `submit(NULL, NULL, NULL,
   NULL)` on zero-copy input. Six dereferenced the picture, five returned
   `-EINVAL`, and `motion_sycl` with `motion_add_uv` read chroma that had never
   been imported.
3. **Filter.** A failed VA-surface import warned and skipped the frame, which
   changed the pooled score without a message. The patch stack also never asked
   `vmaf_feature_backend_twin()` (ADR-1359) for a SYCL twin, which the CLI does.

## NULL-picture table (from the phase research; `test_sycl_zerocopy_guards` holds every row)

| Extractor | Before | After (Stage 1) |
| --- | --- | --- |
| `psnr_sycl`, `psnr_hvs_sycl` (chroma), `ciede_sycl`, `speed_chroma_sycl`, `speed_temporal_sycl`, `ssimulacra2_sycl` | NULL dereference or `-EINVAL` | `-ENOTSUP`, named |
| `float_ssim_sycl`, `ssim_sycl`, `float_ms_ssim_sycl`, `float_psnr_sycl`, `float_adm_sycl`, `float_vif_sycl`, `float_motion_sycl` | NULL dereference or `-EINVAL` | `-ENOTSUP`, named |
| `motion_sycl` with `motion_add_uv` | stale chroma, no error | `-ENOTSUP` |
| CPU extractor named by `feature=` | skipped | `-ENOTSUP` from the pre-pass guard, before any state changes |
| `vif_sycl`, `adm_sycl`, `motion_sycl`, `motion_v2_sycl`, `cambi_sycl`, `float_moment_sycl`, luma `psnr` / `psnr_hvs`, `vmaf_v0.6.1` | work | work, bit-exact |

## Design

- `vmaf_read_pictures_sycl` runs `sycl_check_zero_copy_extractors` first, before
  `vmaf_sycl_queue_wait`, the picture count and the frame advance, so a refusal
  leaves no half-advanced frame.
- Every extractor that needs host pictures calls `vmaf_sycl_require_host_pictures`.
- FFmpeg patch 0005 resolves each `feature=` name through
  `vmaf_feature_backend_twin()` on both input paths. Zero-copy with no usable
  twin aborts configuration with the feature named; host upload falls back to
  the CPU extractor with a warning. QSV surfaces other than NV12 and P010 are
  rejected and a VA import failure is fatal.

## Stage-1 results (Arc A380, `scripts/test/zerocopy-e2e.sh --stage 1 --repeat 5`)

The harness runs each case on CPU `libvmaf` (software decode), `libvmaf_sycl`
with host upload, and `libvmaf_sycl` on zero-copy, on the 576x324 Netflix pair
and the 1920x1080 checkerboard pair, encoded with `hevc_qsv` as NV12 (8 bit)
and P010 (10 bit). Final run: 8 bit `pass=48 fail=0 nonexact=0`, 10 bit
`pass=48 fail=0 nonexact=0`; all 9 stage-1 cases per clip and depth are
bit-exact and repeat-stable over five runs, and the stage-2 and stage-3
cases fail loudly with a message that names the feature (15 cases x 2 clips). Baseline for
later stages: zero-copy `model-vmaf_v0.6.1` on the checkerboard, 60 frames,
78.84 fps at 8 bit and 80.00 fps at 10 bit.

The first run, before any follow-up fix, found three things the plan had not
predicted, all fixed on the same branch:

- The host-upload twins were not the CPU's in two places: `float_motion_sycl`
  emitted no `motion3`, and the CPU SpEED extractors used libm `log2f`, so the
  twins differed from FFmpeg's `libvmaf` by up to 6e-6 (ADR-1462 makes the CPU
  round `log2` correctly).
- `cambi` on zero-copy changed from run to run (2 of 5 runs differed from host
  upload, by up to 2.3). The driver dropped each frame's import once the DMA-BUF
  was mapped at the address the previous import had just freed, under batched
  command lists; ADR-1596 gives the primary queue immediate command lists.
- One CPU output, `VMAF_integer_feature_motion_sad_score`, is written by no SYCL
  motion twin; it is exempt in the comparator and tracked in `docs/state.md`
  (`T-GPU-MOTION-SAD-SCORE-NOT-EMITTED-2026-10-02`).

## Open items

- **Stage 2**: import the Cb/Cr planes of the VA surface (same DMA-BUF object at
  a non-zero offset, same modifier, same pitch as luma) so `psnr`, `psnr_hvs`,
  `ciede`, `speed_*` and `ssimulacra2` run on zero-copy input.
- **Stage 3**: move the float, SSIM, MS-SSIM and SpEED extractors from host
  staging to the shared planes. `float_ms_ssim_sycl` needs one new device kernel
  (plane to float).
- The D3D11 import has no chroma either; readers fail loudly there too.
- `test_sycl_ordered_sum` fails on the A380 independently of this work
  (`T-SYCL-ORDERED-SUM-A380-GARBAGE-2026-10-02`).
