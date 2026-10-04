<!-- markdownlint-disable MD013 MD060 -->
# Research-1595: SYCL zero-copy feature correctness

- **Status**: Active (Stages 1 to 3 done)
- **Workstream**: [ADR-1595](../adr/1595-sycl-zerocopy-fail-loud-twin-routing.md), [ADR-1596](../adr/1596-sycl-va-import-immediate-cmdlist.md), [ADR-1597](../adr/1597-sycl-zerocopy-planar-chroma-import.md), [ADR-1598](../adr/1598-sycl-host-staging-to-shared-planes.md), [ADR-1599](../adr/1599-sycl-float-motion-add-uv.md), phase 12
- **Last updated**: 2026-10-03

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
  twins differed from FFmpeg's `libvmaf` by up to 6e-6. The branch first fixed
  both itself (a correctly rounded CPU `log2`); master then fixed both
  independently (`motion3` in #1914, SpEED in
  [ADR-1477](../adr/1477-speed-upstream-double-math.md), which forms the SpEED
  entropies and score on the host with `speed.c`'s statements, so the twins
  equal the CPU of the same process). The branch's versions were dropped when
  it was rebased onto master.
- `cambi` on zero-copy changed from run to run (2 of 5 runs differed from host
  upload, by up to 2.3). The driver dropped each frame's import once the DMA-BUF
  was mapped at the address the previous import had just freed, under batched
  command lists; ADR-1596 gives the primary queue immediate command lists.
- One CPU output, `VMAF_integer_feature_motion_sad_score`, is written by no SYCL
  motion twin; it is exempt in the comparator and tracked in `docs/state.md`
  (`T-GPU-MOTION-SAD-SCORE-NOT-EMITTED-2026-10-02`).

## Chroma descriptor probe (A380)

`VMAF_SYCL_IMPORT_DEBUG=1` now dumps every exported layer and object. Four
probes on the Arc A380 (i915, iHD), `hevc_qsv` clips, two frames each, QSV
zero-copy `libvmaf_sycl` ([ADR-1597](../adr/1597-sycl-zerocopy-planar-chroma-import.md)).
All four report `num_layers=2 num_objects=1`, `obj=0` for both layers,
`num_planes=1` per layer, modifier `0x0100000000000009` (Tile4).

| Shape | layer[0] (Y) drm_format, pitch | layer[1] (UV) drm_format, offset, pitch | object size |
| --- | --- | --- | --- |
| NV12 576x324 | `R8` 0x20203852, 640 | `GR88` 0x38385247, 225280, 640 | 348160 |
| NV12 1920x1080 | `R8`, 1920 | `GR88`, 2088960, 1920 | 3133440 |
| P010 576x324 | `R16` 0x20363152, 1152 | `GR1616` 0x32335247, 405504, 1152 | 626688 |
| P010 1920x1080 | `R16`, 3968 | `GR1616`, 4317184, 3968 | 6475776 |

- **A1** (chroma is the same object at a non-zero offset with the same
  modifier): holds. The UV offset is the luma pitch times the luma height
  rounded up to 32 rows (352 and 1088 rows), a multiple of 4096.
- **A2** (`layers[1].pitch == layers[0].pitch`, `drm_format` is `GR88` for
  NV12 and `GR1616` for P010): holds in all four probes.
- The object size equals `offset + ceil(ch/32) * (pitch/128) * 4096` in every
  probe (for example 4317184 + 17 x 31 x 4096 = 6475776), which is the
  whole-tile extent the validator checks.
- The pitch exceeds the row bytes for 576 wide NV12 (640 vs 576) and 1920 wide
  P010 (3968 vs 3840), so the kernel and its test must honour `pitch` and a
  partial last tile column.
- Only Tile4 was observed on this device; LINEAR and Y-tiled stay covered by the
  host-synthesised vectors, not by hardware.

No shape contradicts the planned kernel inputs; the design is unchanged.

## Stage-2 results (Arc A380, `scripts/test/zerocopy-e2e.sh --stage 2 --bench --repeat 5`)

The VA import now writes the UV layer into the shared Cb and Cr planes on
every frame (ADR-1597): a layout-addressed de-interleave kernel on the DMA-BUF
path, the same kernel after a staged copy on the readback path, with the P010
shift applied once in that kernel. Same harness, clips and depths as Stage 1.

| Depth | Summary | Stage-3 cases | Zero-copy repeat stability |
| --- | --- | --- | --- |
| 8-bit NV12 | `pass=48 fail=0 nonexact=0` | 26 of 26 `PASS loud-fail` | 5 runs per case equal host upload, no `zc-nondeterministic` |
| 10-bit P010 | `pass=48 fail=0 nonexact=0` | 26 of 26 `PASS loud-fail` | 5 runs per case equal host upload, no `zc-nondeterministic` |

`psnr` (3 metrics) and `psnr_hvs` (4 metrics) are bit-exact against the CPU and
against host upload on both clips. Logs: `12-09-e2e-stage2-{8,10}bit.log`.

- **`motion_add_uv`** has no CPU leg: the CPU integer `motion` has no such option
  (only `float_motion` does, covered by `float_motion_uv` at stage 3), so a
  three-leg comparison cannot be built. It is the harness case `motion_uv`
  (decision D-11) with a declared host-upload reference: real QSV zero-copy
  against host upload of the same `motion_sycl` twin (itself held to the ADR-1326
  fixed-point oracle), a weaker statement than "equals the CPU". Run with
  `--repeat 5` at 8 and 10 bit on both clips: 0 mismatches in
  `integer_motion2_mau` and `integer_motion3_mau` at full precision, 5 repeats
  each (the 12-08 ad-hoc script that first showed this is now the harness case),
  plus `test_sycl_zerocopy_parity` on emulated zero-copy, which also asserts that
  chroma changes the score.
- **Layouts.** Only the Tile4 modifier (`0x0100000000000009`, layer 1 `GR88` /
  `GR1616`, `num_layers=2 num_objects=1`) was seen on the A380. LINEAR and
  Y-tiled chroma are covered by the host-synthesised vectors in
  `test_sycl_chroma_import`, not by hardware.
- **Stage-1 regression.** `--stage 1` on the same build reports `pass=44 fail=4
  nonexact=0` per depth. The four failures are `psnr` and `psnr_hvs` on both clips
  returning `unexpected-success` ("zero-copy succeeded before its stage"): the
  comparator's stage gate working as designed now that those cases work. Every
  other case is unchanged and bit-exact. Logs: `12-09-e2e-stage1-regress-*.log`.
- **D3D11** import stays luma only (RESEARCH Q6, out of scope); its chroma readers
  fail with `needs chroma planes, which this zero-copy import did not provide`.

## D-01 cost (A380)

D-01 imports chroma unconditionally and allocates the chroma planes whenever the
frame buffers are, so a luma-only run (the default model) pays for them.

Memory, from `vmaf_sycl_shared_chroma_init` (8 device planes, 4 pinned host
staging planes, each `ceil(w/2) x ceil(h/2)` samples of 1 byte at 8 bit and 2 at
10 bit):

| Frame | Device, 8-bit | Pinned, 8-bit | Device, 10-bit | Pinned, 10-bit |
| --- | --- | --- | --- | --- |
| 1920x1080 | 4.1 MB | 2.1 MB | 8.3 MB | 4.1 MB |
| 3840x2160 | 16.6 MB | 8.3 MB | 33.2 MB | 16.6 MB |

The device figure is half the size of the shared luma buffers (four buffers of
`w x h` samples). These are computed from the allocation sizes, not read from
the driver.

Throughput, zero-copy `model-vmaf_v0.6.1` on the checkerboard (60 frames, same
`--bench` run position as the baseline):

| Depth | 12-05 baseline (Stage 1) | Stage-1 re-run on this build | Stage-2 run | Stage-2 vs baseline |
| --- | --- | --- | --- | --- |
| 8-bit | 78.84 fps | 78.53 fps | 80.32 fps | +1.9 % |
| 10-bit | 80.00 fps | 76.53 fps | 76.92 fps | -3.9 % |

Reading it: the flag threshold is a 10 % slowdown on checkerboard 1080p, and it did
not fire. 8 bit is within noise. 10 bit is about 4 % lower in both re-runs on this
build; the superseded first 12-05 10-bit run (earlier code) gave 77.42 fps, so the
baseline itself spreads by 3 %, and a 60-frame run is 0.75 s long.
The figure depends strongly on where in a run it is taken: a stand-alone
single-case run of the same command measured 58.7 to 61.9 fps (6 runs,
`12-09-bench-noise.log`), about 25 % lower than inside the full run, so only the
in-run comparison above is meaningful. The 10-bit chroma import moves twice the
bytes of the 8-bit one, which would fit a small real cost; this run cannot separate
it from noise, and a pre-D-01 build in the same run position would be needed to
say. No off-switch was added (D-01). 4K was not measured: the only 4K P010 source at
hand is a single frame (`blue_sky_1frame_3840x2160_10b.yuv`), too short to time.

## Stage-3 results (Arc A380, `scripts/test/zerocopy-e2e.sh --stage 3 --bench --repeat 5`)

Every extractor that staged host pictures now reads the shared planes
([ADR-1598](../adr/1598-sycl-host-staging-to-shared-planes.md)), `float_ms_ssim_sycl`
converts them on the device with `plane_to_float()`, and `float_motion_sycl` takes
`motion_add_uv` ([ADR-1599](../adr/1599-sycl-float-motion-add-uv.md)). Full set, both
clips (src01 576x324 48 frames, checkerboard 1080p 60 frames), five zero-copy runs each:

| Depth | Cases | Result |
| --- | --- | --- |
| 8-bit NV12 | 48 | `pass=48 fail=0 nonexact=0`, no `loud-fail`, no `zc-nondeterministic` |
| 10-bit P010 | 48 | `pass=48 fail=0 nonexact=0`, no `loud-fail`, no `zc-nondeterministic` |

Pooled model scores, CPU / host upload / zero-copy (identical to the printed digits in
all three legs):

| Clip | Depth | `vmaf_v0.6.1` | `vmaf_float_v0.6.1` |
| --- | --- | --- | --- |
| src01 | 8 | 75.953252 | 75.950949 |
| checkerboard | 8 | 39.844822 | 39.846317 |
| src01 | 10 | 76.058344 | 76.057651 |
| checkerboard | 10 | 39.806130 | 39.806468 |

- `float_ms_ssim`: `test_sycl_zerocopy_parity` rows (luma, `enable_lcs`,
  `enable_chroma`; 8 and 10 bit) are `==` between CPU, host upload and zero-copy, and
  the e2e case passes on both clips.
- `float_motion_uv` (`float_motion` with `motion_add_uv=true`) could not run on zero-copy
  until `float_motion_sycl` declared the option: the twin refused it, and zero-copy has no
  CPU fallback. It now equals the CPU on host upload and host upload on zero-copy
  (parity test `==`, e2e pass on both clips and depths). Decision D-10.
- Stage-1 and Stage-2 runs now report `unexpected-success` for the cases that moved to
  stage 3 or earlier: that is the harness doing its job, not a regression.
- Throughput, `vmaf_v0.6.1` on checkerboard 1080p 8-bit, same session, stage-2 library
  (dd00f1b59) against this build, four alternating runs each: 73.7 fps (62.8, 75.1,
  78.3, 78.6) against 77.8 fps (78.7, 78.3, 79.7, 74.6), no regression. No stage-3 change
  touches the integer `vif` / `adm` / `motion` path that model uses. In-run `--bench`
  figures of the final run: 76.05 fps (8-bit), 76.24 fps (10-bit); the stage-2 run gave
  80.32 and 76.92, in a different session (the figure moves 5 % between sessions).

## Full-precision proof (`score_fmt=%.17g`, decision D-11)

The harness's verdicts were first computed from logs written at the filter's default
`%.6f`, so "exact" meant equal to six decimals. Every leg now runs with
`score_fmt=%.17g` (patch 0016, the same option on `libvmaf` and `libvmaf_sycl`) and
the comparator compares the full doubles. Arc A380, `--stage 3 --bench --repeat 5`,
both clips, 25 cases each (the 24 of the stage-3 run plus `motion_uv`), so 50 verdicts per depth:

| Depth | Result | Notes |
| --- | --- | --- |
| 8-bit NV12 | `pass=50 fail=0 nonexact=0` | `ciede` on src01: host vs CPU 1.112e-11 on one frame, inside the declared 1e-9 bound |
| 10-bit P010 | `pass=50 fail=0 nonexact=0` | every case identical to the CPU, `ciede` included |

Zero-copy equals host upload exactly on every case, clip and depth, on all five runs
(no `zc-nondeterministic`); every other host-vs-CPU comparison is bit-exact. The one
number that was invisible at `%.6f` is the `ciede` residual, the libm difference
already recorded for the CUDA, SYCL and HIP twins
(`T-CUDA-CIEDE-LIBM-RESIDUAL-2026-10-01`, ADR-1436); it is declared in the comparator
as the parity gate's own `LIBM_TWINS` cell rather than as a new tolerance. In-run
`--bench` figures: 79.58 fps (8-bit), 72.64 fps (10-bit).

## Re-validation after the rebase onto master (2026-10-03)

The branch was rebased from `397997897` onto `VMAFx/master` `b01ffe42d` (104
master commits, among them the SYCL raster-order sums of `float_ssim` and
`float_ms_ssim`, strict FP in every translation unit, the Xe2 sub-group sizes
and master's own `motion3` and SpEED fixes). The branch's ADRs moved to
ADR-1504 to ADR-1508 (now ADR-1595 to ADR-1599) and this digest from Research-1461. Three branch commits
were dropped because master fixed the same thing first: `float_motion_sycl`'s
`motion3` (#1914), the correctly rounded CPU SpEED `log2` with its ADR
(master's [ADR-1477](../adr/1477-speed-upstream-double-math.md) forms the
SpEED entropies and score on the host with `speed.c`'s statements, so the twin
equals the CPU of the same process), and the `VmafSyclState` aggregate
refactor. The shared-plane reads were merged onto master's exact arithmetic:
`float_ssim` and `float_ms_ssim` read the shared planes and then add their terms
in master's raster order; the VA chroma import sits on master's split of
`dmabuf_import.cpp` (exception-guarded de-tile, deferred free).

Arc A380, `scripts/test/sycl-dev-container.sh` (JIT build from scratch,
`UR_L0_USE_IMMEDIATE_COMMANDLISTS=0`):

| Check | Result |
| --- | --- |
| `meson test --suite sycl` | 70 of 70 |
| `meson test --suite fast` | 351 OK, 1 skipped, 1 failure: `test_icx_system_libm` (recorded as `T-SYCL-LD-BIND-NOW-LIBIMF-IFUNC-2026-10-03` on `feat/vmafx-container-hybrid-toolchain`), which fails the same way on a master build in the same container |
| `zerocopy-e2e.sh --stage 3 --bench --repeat 5`, 8-bit NV12 | `pass=50 fail=0 nonexact=0`, 79.89 fps |
| same, 10-bit P010 | `pass=50 fail=0 nonexact=0`, 73.17 fps |
| Netflix golden gate (GCC 15.2 golden profile, ADR-1317) | 280 passed, 3 skipped, assertions untouched |

Every case is identical between zero-copy and host upload on all five runs,
SpEED included. Host upload equals the CPU on every case except `ciede`, inside
the declared 1e-9 cell: 1.111e-11 on src01 at 8 bit as before, and now 2.835e-12
on src01 at 10 bit (identical before the rebase). Master's builds link glibc's
`libm` in icx builds ([ADR-1495](../adr/1495-icx-system-libm.md)), so the CPU
`ciede` leg's `powf` changed library; the twin's arithmetic is master's.

## Re-validation after the rebase onto master `7fdefb4e8` (2026-10-04)

The branch was rebased onto master `7fdefb4e8` with
`feat/vmafx-container-hybrid-toolchain` (ADR-1439, ADR-1593, ADR-1594)
underneath. Master and other open branches had taken ADR numbers 1504 to 1508
and 1561 to 1568 in the meantime, so the branch's ADRs moved (1504 to 1508, then
1564 to 1568) to ADR-1595 to ADR-1599 and this digest to Research-1595.
Master had moved the SYCL backend page into sub-pages; the zero-copy behaviour
now lives in `docs/backends/sycl/zero-copy.md`. The three zero-copy test
executables pass ADR-1593's `test_link_kwargs`.

Arc A380, `scripts/test/sycl-dev-container.sh` (JIT build from scratch,
`UR_L0_USE_IMMEDIATE_COMMANDLISTS=0`):

| Check | Result |
| --- | --- |
| `meson test --suite sycl` | 70 of 70 |
| `meson test --suite fast` | 360 OK, 1 skipped, 0 failures (`test_sycl_ordered_sum` passes with the probe fix) |
| `zerocopy-e2e.sh --stage 3 --bench --repeat 5`, 8-bit NV12 | `pass=50 fail=0 nonexact=0`, 76.92 fps |
| same, 10-bit P010 | `pass=50 fail=0 nonexact=0`, 76.34 fps |
| Netflix golden gate (GCC golden profile, ADR-1317) | 280 passed, 3 skipped |
| FFmpeg series replay on `n9.0.2` | 20 of 20 patches, `libvmaf` and `libvmaf_sycl` filters built |

## Open items

- **Stage 2** (done, above): chroma import for `psnr`, `psnr_hvs`, `motion_add_uv`.
  `ciede`, `speed_*` and `ssimulacra2` read host pictures and move with stage 3.
- **Stage 3** (done, above): the float, SSIM, MS-SSIM, ciede, ssimulacra2 and SpEED
  extractors read the shared planes; `float_motion_sycl` gained `motion_add_uv`.
- The D3D11 import has no chroma (out of scope); readers fail loudly there too.
- `test_sycl_ordered_sum` failed on the A380 because its probe freed the host
  sources of a deferred copy; fixed on this branch
  (`T-SYCL-ORDERED-SUM-A380-GARBAGE-2026-10-02`,
  `T-SYCL-ORDERED-SUM-MALLOC-PERTURB-2026-10-03`).
