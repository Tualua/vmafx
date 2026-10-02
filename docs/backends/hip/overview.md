<!-- markdownlint-disable MD060 -->
# HIP (AMD ROCm) compute backend

> **Status (2026-05-18):** `vmaf --backend hip` is end-to-end working on AMD
> ROCm hosts following [ADR-0519](../../adr/0519-hip-import-state-implementation.md).
> The library-side `vmaf_hip_import_state` was promoted from `-ENOSYS` to a
> real implementation; the CLI now produces a valid VMAF JSON on any AMD GPU
> visible to ROCm. HIP joins CUDA / SYCL / Metal as a fully working
> runtime-selected backend. (The Vulkan backend was removed in ADR-0726.)
>
> **Measured parity (2026-09-27):** with the published `v1.0.0-rc.1-rocm10`
> image on AMD gfx1036 (the Radeon iGPU of a Granite Ridge CPU), the Netflix
> golden src01 pair and `vmaf_v0.6.1`, HIP scores 76.667848 against the CPU's
> 76.667831, a pooled difference of 1.67e-5. Per frame, `motion2` and
> `motion3` differ by up to 1.26e-5 and VIF by up to 5.4e-7; ADM runs on the
> CPU (see `integer_adm_hip` below). This is close agreement, not
> bit-exactness, and it is inside the 5e-5 `places=4` cross-backend gate
> from [ADR-0214](../../adr/0214-gpu-parity-ci-gate.md). The motion
> difference came from `motion_hip` blurring each frame instead of the frame
> difference; since 2026-09-30 it runs the CPU's arithmetic and matches the
> CPU exactly (measured on the same gfx1036 on 2026-10-01, see
> [RC3 CPU parity](#rc3-cpu-parity-motion-tiny-frames-and-cpu-options-2026-09-30)).
> VIF is then the only difference: 76.667849 against 76.667831, 1.79e-5
> pooled and at most 4.9e-5 per frame, slightly more than before because the
> old motion error partly offset the VIF one.
>
> **Dispatch posture (2026-05-18, updated per
> [ADR-0530](../../adr/0530-hip-feature-flag-promotion-and-picture-buffer.md)):**
> `vmaf_fex_integer_motion_hip` now carries
> `VMAF_FEATURE_EXTRACTOR_HIP` and is selectable from the
> model-driven dispatch when a HIP state has been imported
> (`vmaf --backend hip` implies that import). The
> `VMAF_PICTURE_BUFFER_TYPE_HIP_DEVICE` enum entry has been added
> for the future HIP picture pool; pictures still arrive as
> `VMAF_PICTURE_BUFFER_TYPE_HOST` for now and the HIP feature TUs
> perform their own HtoD copies (`hipMemcpy2DAsync`). End-to-end
> verification: `--backend hip --feature integer_motion` produces
> a clean VMAF JSON with VMAF = 76.71 on the Netflix src01 pair
> (vs CPU 76.67: a 0.04 gap, far outside the 5e-5 places=4
> cross-backend gate from
> [ADR-0214](../../adr/0214-gpu-parity-ci-gate.md); the measured
> parity at the top of this page supersedes it); 48
> `hipModuleLaunchKernel(calculate_motion_score_kernel_8bpc)`
> launches per 48-frame clip confirm the HIP kernel is actually
> dispatching.
>
> **Status (2026-09-18):** 18 of 19 registered HIP extractors carry
> active GPU flags (`VMAF_FEATURE_EXTRACTOR_HIP` / `VMAF_FEATURE_EXTRACTOR_TEMPORAL`)
> and execute on AMD GPU hardware. Each has been validated via device parity tests
> against the CPU reference implementation.
>
> `integer_ssim_hip` joined them on 2026-09-18. Its kernel used to be an 11-tap
> float Gaussian, 4.5e-3 away from the CPU `ssim`, so it was kept out of dispatch
> (ADR-0564). It now runs the CPU's 9-tap int64 kernel, ported from the CUDA
> twin; see [integer_ssim_hip](#integer_ssim_hip) below.
>
> **Fixed (2026-09-19):** HIP extractors used to upload the host pictures with
> an asynchronous copy and return without waiting for it. On a multi-frame run
> the picture buffer was refilled with the next frame while the copy still read
> it, so frames were scored against the next frame's samples: a different set
> on every run with one extractor, and the same 46 of 48 frames on every run
> with several in one process. It affected `ciede_hip`, `float_adm_hip`,
> `float_moment_hip`, `float_psnr_hip`, `float_ssim_hip`, `float_vif_hip`,
> `psnr_hip` and `vif_hip` (up to 10.7 dB on `float_psnr`, 0.30 on `vif`).
> Scores from a multi-frame HIP run made before this fix should be recomputed.
> Every upload now waits until it has read the picture; see
> [Picture uploads](#picture-uploads) below.
>
> One extractor legitimately retains `.flags = 0` (silently falling back to CPU):
>
> - `integer_adm_hip`: Lacks internal HtoD picture staging buffers and passes host
>   pointers directly into device kernels. Flags remain cleared until picture staging
>   (~350 LOC) or the HIP device picture pool (T7-10c, ~600 LOC) lands.
>
> | Extractor | Feature name | GPU Active | Added in |
> | --- | --- | --- | --- |
> | `integer_psnr_hip` | `psnr_hip` | Yes | ADR-0241 |
> | `float_psnr_hip` | `float_psnr_hip` | Yes | ADR-0254 |
> | `ciede_hip` | `ciede_hip` | Yes | ADR-0259 / PR #1016 / ADR-1448 (the CPU's arithmetic) |
> | `float_moment_hip` | `float_moment_hip` | Yes | ADR-0260 |
> | `integer_motion_v2_hip` | `motion_v2_hip` | Yes | ADR-0267 |
> | `float_motion_hip` | `float_motion_hip` | Yes | ADR-0373 |
> | `float_ssim_hip` | `float_ssim_hip` | Yes | ADR-0375 |
> | `float_vif_hip` | `float_vif_hip` | Yes | ADR-0379 |
> | `integer_psnr_hvs_hip` | `psnr_hvs_hip` | Yes | PR #995 |
> | `integer_cambi_hip` | `cambi_hip` | Yes | PR #996 / ADR-1378 (device-resident) |
> | `ssimulacra2_hip` | `ssimulacra2_hip` | Yes | PR #1000 |
> | `integer_vif_hip` | `integer_vif_hip` | Yes | PR #1001 |
> | `integer_motion_hip` | `integer_motion_hip` | Yes | PR #1004 |
> | `integer_adm_hip` | `integer_adm_hip` | Deferred | PR #1007 |
> | `integer_ms_ssim_hip` | `ms_ssim_hip` | Yes | ADR-0285 / PR #1013 |
> | `integer_ssim_hip` | `integer_ssim_hip` | Yes | PR #999 / ADR-0564 |
> | `float_adm_hip` | `float_adm_hip` | Yes | ADR-0468 / PR #1024 |
> | `speed_chroma_hip` | `speed_chroma_hip` | Yes | ADR-0567 / ADR-0852 / ADR-1384 (device-resident) |
> | `speed_temporal_hip` | `speed_temporal_hip` | Yes | ADR-0567 / ADR-0852 / ADR-1384 (device-resident) |
>
> All registered kernels require `enable_hip=true` + `enable_hipcc=true`.
> Without `enable_hipcc=true`, `float_ssim_hip`, `integer_ssim_hip`, and
> `vmaf_hip_picture_alloc` log an informative error naming `-Denable_hipcc=true`
> before returning `-ENOSYS`. Pre-compiled HSACO fat binaries are not bundled
> without `hipcc` because AMD ROCm requires target-specific HSACO code objects.

## integer_ssim_hip

`integer_ssim_hip` publishes the same `ssim` feature as the CPU `ssim`
extractor (`integer_ssim.c`) and computes it the same way:

- a 9-tap Gaussian with integer weights `[2, 9, 28, 55, 68, 55, 28, 9, 2]`;
- int64 sums for the moments, which makes them exact;
- near the frame border the window is truncated to the taps inside the frame,
  as on the CPU;
- the per-pixel SSIM term in double, built with `-ffp-contract=off` so that it
  rounds like the CPU's.

The frame sum is the CPU's as well, at every frame size
([ADR-1438](../../adr/1438-hip-ssim-cpu-frame-sum.md)): the device writes one
term per pixel, the host reads the plane back and adds it row by row, as
`calc_ssim()` does. A sum of doubles depends on its order, so the terms are
never added on the device; only the window weights, which are integers, are
reduced per block. The score is therefore the CPU's to the last bit, at every
bit depth, with and without `enable_db` / `clip_db`. Any frame size is
accepted.

Measured on a gfx1036 at `--precision max` against `--backend cpu`: 178 of 178
frames identical (the Netflix 576x324 pair at 8, 10, 12 and 16 bits and as
10-bit 4:2:2, both 1920x1080 checkerboard pairs, Sparks 480x270 at 10 bits, 48
frames of BBB 3840x2160, full-range noise at four depths, a bright 16-bit
1080p pair). Until 2026-10-01 only frames of at most 4096 pixels were summed
in the CPU's order
([ADR-1400](../../adr/1400-hip-integer-ssim-raster-sum-small-frames.md));
larger frames were reduced per 16x8 block and 1 of those 178 frames matched,
the others up to 1.1e-11 away (on the 1080p checkerboard whose score is -0.53,
where terms of both signs cancel).

The read-back costs time and memory: 30.0 ms instead of 28.2 ms per 1920x1080
frame and 98.1 ms instead of 94.3 ms per 3840x2160 frame (medians of 21
interleaved pairs of runs under other load), and 8 bytes per pixel of device
and of pinned host memory (66 MB each at 3840x2160).

How it gets selected:

- **Models.** When a model lists `ssim` and the HIP backend is active
  (`--backend hip`, or `vmaf_hip_import_state()` in the C API), the HIP twin
  computes it, with `enable_db` and `clip_db` if the model sets them.
- **CLI `--feature`.** `--backend hip --feature ssim` runs the HIP twin too
  ([ADR-1359](../../adr/1359-cli-feature-backend-twin.md)); the JSON output
  names the extractor that ran under `feature_backends`. Naming the twin
  always registers it:

```bash
vmaf --reference ref.yuv --distorted dist.yuv \
     --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
     --backend hip --feature integer_ssim_hip \
     --no_prediction --json --output ssim.json
```

The output has one `ssim` value per frame, as with `--feature ssim`.

## integer_ms_ssim_hip

`integer_ms_ssim_hip` publishes the `float_ms_ssim` feature of the CPU
extractor `float_ms_ssim` and returns the same numbers, bit for bit: the
score and, with `enable_lcs=true`, the 15 per-scale means
`float_ms_ssim_{l,c,s}_scale{0..4}`. `--backend hip` selects it for
`--feature float_ms_ssim` and for a model that lists the feature:

```bash
vmaf --reference ref.yuv --distorted dist.yuv \
     --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
     --backend hip --feature float_ms_ssim=enable_lcs=true \
     --no_prediction --json --output ms_ssim.json --precision max
```

It takes the CPU's options (`enable_lcs`, `enable_db`, `clip_db`;
`enable_chroma` is accepted and scores luma only) and the CPU's minimum
frame size of 176x176.

The kernels compute each sample the way the CPU does
([ADR-1403](../../adr/1403-cuda-strict-fp-every-kernel.md), which made the
CUDA twin exact first):

- the decimation between pyramid levels fuses every tap, as
  `ms_ssim_decimate.c` does;
- the Gaussian window sums are fp32 products added without rounding and
  rounded to fp32 once per pass, which is what the CPU's fp64 sum gives. The
  device carries the sum as an fp32 pair, because fp64 arithmetic is slow on
  it: an fp64 sum gave the same scores and 299 instead of 173 ms per
  3840x2160 frame;
- luminance and contrast divide an fp64 numerator by an fp32 denominator and
  structure is an fp32 quotient, as in `iqa/ssim_tools.c`;
- the host rounds each per-scale mean to fp32 and combines the scales with
  `fabs()` on all three terms, as `ms_ssim.c` does.

Measured on a gfx1036 against `--backend cpu` at `--precision max` with
`enable_lcs=true`: the Netflix 576x324 pair (48 frames), the same pair at 10
bits (3), both 1080p checkerboard pairs (3 each) and BBB 3840x2160 (50) give
1712 values, all identical. Before, 6 were and no score: it was up to 3.0e-6 off
and a per-scale mean up to 2.1e-5. `enable_db` / `clip_db` scores are
identical as well. The exact sums cost time on this device: `float_ms_ssim` goes
from 30.3 to 36.9 ms per 1920x1080 frame and from 158 to 169 ms per 3840x2160
frame (medians of seven interleaved runs under other load; a second set of
nine gave 32.4 to 39.5 and 126 to 137).

The one thing left that is not the CPU's is the order in which the device adds
the l, c and s terms of a scale (per block, where the CPU adds in raster
order). The sums are fp64 and each mean is rounded to fp32, so the orders
would have to differ by about one part in 10^8 of a mean to show; they differ
by about one part in 10^14.

`test_hip_ms_ssim_arith` replays the kernels' arithmetic on the host against
the CPU extractor and needs no AMD device; `test_hip_ms_ssim_parity` compares
on one.

## Building

ROCm 7.0 or later is required; **10.0.0** is the version tested in CI, in the
dev container, and in the published GPU images (ADR-1225).

ROCm 10 has no apt channel — since ROCm 7.14 AMD builds and releases through
"TheRock", and `repo.radeon.com/rocm/apt/` tops out at 7.2.4. The fork
therefore installs ROCm from the digest-pinned
`rocm/dev-ubuntu-26.04:10.0.0-full` container image. On a workstation, either
use your distribution's ROCm packages (any 7.0+ release builds this fork) or
run the dev container; CI uses `scripts/ci/install-rocm-from-image.sh`, which
streams the image's `/opt/rocm` out of the registry without a 29 GB
`docker pull`.

```bash
meson setup build -Denable_cuda=false -Denable_sycl=false \
                  -Denable_hip=true -Denable_hipcc=true
ninja -C build
python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- -C build
```

`enable_hipcc=false` (the default) compiles the HIP C host runtime but
skips the `hipcc`-compiled kernel objects; every extractor returns
`-ENOSYS` at `init()`. Set both flags to `true` to compile and link the
real device kernels.

The scaffold has **zero hard runtime dependencies** — no ROCm SDK,
no `hipcc`, no `amdhip64`. The Meson build files include an optional
`dependency('hip-lang', required: false)` probe so a host that already
has ROCm installed will see the dependency resolve; the scaffold compiles
cleanly without it.

### `-Dhip_gfx_targets` (HSACO fat-binary targets)

`hipcc --genco` produces an HSACO blob for each `--offload-arch`
target. The Meson build discovers the targets in this order:

1. The `-Dhip_gfx_targets=<csv>` operator override (explicit).
2. `rocm_agent_enumerator` (filters to `gfx*` lines).
3. `hipconfig --amdgpu-target`.
4. The hard-coded fallback list
   `gfx90a,gfx1030,gfx1036,gfx1100`
   (CDNA2 server + RDNA2 desktop + AMD Raphael APU iGPU + RDNA3).

Steps 2 and 3 only succeed when the build host can see a real GPU.
Inside a no-GPU build sandbox (BuildKit, CI) both probes return
empty and the build falls through to step 4. The fallback was
`gfx90a` only until [ADR-0561](../../adr/0561-hip-gfx-targets-fallback-widening.md);
that narrow fallback shipped libvmaf.so binaries that failed at
runtime on the fork's own dev host (AMD Raphael APU `gfx1036`) with
`hip_fatbin.cpp: No compatible code objects found for: gfx1030`.
(That host needed `HSA_OVERRIDE_GFX_VERSION=10.3.0` to alias `gfx1036`
onto the allowlisted `gfx1030` under ROCm 6.x/7.x. ROCm 10 supports
`gfx1036` natively, so the override is gone — ADR-1225.) Widening the
fallback closed that failure mode without changing what an operator with a
configured GPU sees.

Operators that need a smaller fat binary (image size, build time)
can pin a single target:

```bash
meson setup build -Denable_hip=true -Denable_hipcc=true \
                  -Dhip_gfx_targets=gfx1036
```

Multi-target operator overrides take a comma-separated list (one
`--offload-arch` per target):

```bash
meson setup build -Denable_hip=true -Denable_hipcc=true \
                  -Dhip_gfx_targets=gfx90a,gfx1100
```

The `HIP HSACO targets:` line in the Meson configure output shows
the resolved list for the current build.

## Runtime

When built with HIP and device kernels, the backend is available for
explicit opt-in:

```bash
./build/tools/vmaf --feature psnr_hip --reference ref.yuv ...
./build/tools/vmaf --feature float_psnr_hip --reference ref.yuv ...
./build/tools/vmaf --feature integer_vif_hip --reference ref.yuv ...
./build/tools/vmaf --feature integer_adm_hip:adm_skip_scale0=true --reference ref.yuv ...
```

A twin named this way runs on the thread that calls `vmaf_read_pictures()`,
with or without `--threads`. Until 2026-10-01 `adm_hip` and `float_vif_hip`,
the two twins `--backend hip` does not select for their CPU names, failed
with `problem flushing context` whenever `--threads` was given, because the
worker pool tried to run them.

FFmpeg backend selector: `hip_device=N` (patch `0011-libvmaf-wire-hip-backend-selector.patch`
in `ffmpeg-patches/`; see [ADR-0380](../../adr/0380-ffmpeg-patches-hip-backend-selector.md)).

## Source layout

```text
core/src/hip/                  # HIP runtime (common, picture_hip, dispatch_strategy)
core/src/feature/hip/          # per-feature kernels
  integer_psnr_hip.c              # uint64 atomic-SSE warp-64 __shfl_down
  float_psnr_hip.c                # float (ref-dis)^2, exact integer sum per block
  float_motion_hip.c              # 5x5 Gaussian blur + per-block float SAD
  float_moment_hip.c              # four uint64 atomic accumulator kernel
  float_ssim_hip.c                # two-pass separable 11-tap Gaussian kernel
  float_vif_hip.c                 # multi-scale VIF float pipeline
  float_adm_hip.c                 # ADM float pipeline (ADR-0468)
  ciede_hip.c                     # YUV->Lab, CIEDE2000 dE in fp32 pairs, one float per pixel
  integer_motion_v2_hip.c         # raw-pixel ping-pong, host motion2/motion3 fold
  integer_motion_hip.c            # raw-pixel ping-pong, host motion2/motion3 fold
  integer_motion_sad_hip.c        # diff-first SAD launcher both motion twins call
  hip_tile_index.h                # tile-load index clamp shared by kernels and host tests
  integer_moment_hip.c            # four uint64 atomic accumulator (integer)
  integer_psnr_hvs_hip.c          # PSNR-HVS frequency-weighted distortion
  integer_ssim_hip.c              # 9-tap int64 moments + per-pixel SSIM (CPU kernel)
  integer_ms_ssim_hip.c           # multi-scale SSIM (5 scales, biorthogonal LPF)
  integer_adm_hip.c               # ADM DWT2 + CSF + CM + decouple pipeline
  integer_vif_hip.c               # multi-scale VIF integer pyramid
  integer_cambi_hip.c             # CAMBI banding detection
  ssimulacra2_hip.c               # SSIMULACRA2 (whole frame on the device, ADR-1390)
  adm_hip.c                       # stub — returns -ENOSYS (legacy API)
  vif_hip.c                       # stub — returns -ENOSYS (legacy API)
  motion_hip.c                    # stub — returns -ENOSYS (legacy API)
```

## Kernel notes

- **`integer_psnr_hip`** — uint64 atomic-SSE kernel, warp-64 `__shfl_down`
  reduction. Emits `psnr_y`.
- **`float_psnr_hip`** — the CPU's float (ref-dis)² per pixel, added as an
  integer per 16x16 block, so the sum is exact and the score is the CPU's bit
  for bit at 8 to 16 bits (ADR-1440; see [PSNR](../../metrics/psnr.md#float_psnr)).
  Emits `float_psnr`.
- **`float_motion_hip`** — temporal extractor. 5×5 separable Gaussian blur +
  per-block float SAD partials, blur ping-pong (`blur[2]`), first-frame
  `compute_sad=0` short-circuit, motion2 / motion3 tail emission in `flush()`.
  Emits `VMAF_feature_motion_score`, `VMAF_feature_motion2_score` and
  `VMAF_feature_motion3_score`, and takes every CPU `float_motion` option;
  see [float_motion_hip options](#float_motion_hip-options) below.
- **`float_moment_hip`** — four uint64 atomic accumulator kernel (ref1st,
  dis1st, ref2nd, dis2nd), warp-64 two-uint32-shuffle reduction. Host divides
  by w×h. Emits four `float_moment_*` features, bit-identical to the CPU's
  (see
  [float_moment_hip returns the CPU's moments](#float_moment_hip-returns-the-cpus-moments-bit-for-bit-2026-10-02)).
- **`float_ssim_hip`** — two-pass separable 11-tap Gaussian kernel. Pass 1
  (horiz): five intermediate float buffers over (W-10)×H. Pass 2 (vert + SSIM
  combine): per-block float partial sum over (W-10)×(H-10). Host accumulates in
  double. Emits `float_ssim`. Above scale 1 a decimation kernel runs first and
  W×H is the decimated size; see
  [float_ssim_hip at 1080p and 4K](#float_ssim_hip-at-1080p-and-4k).
- **`ciede_hip`** — the six Y/U/V planes on the device, per-pixel YUV→Lab
  conversion and CIEDE2000 ΔE in the CPU's arithmetic, evaluated on pairs of
  `float` values (ADR-1448), one float per pixel read back, the host's sum in
  the CPU's order and log10 transform. Within 1.4e-11 of the CPU extractor.
  See [ciede_hip](#ciede_hip-follows-the-cpus-arithmetic-2026-10-02). Emits
  `ciede2000`.
- **`integer_motion_v2_hip`** — temporal extractor. Raw-pixel ping-pong (`pix[2]`),
  separable 5-tap Gaussian diff filter with arithmetic right-shift (critical for
  bit-exactness vs CPU — see ADR-0138/0139 and PR #587 AVX2 srlv_epi64 regression),
  single int64 atomic SAD accumulator, host-side `min(cur, next)` fold in `flush()`.
  Emits `VMAF_integer_feature_motion_v2_sad_score` +
  `VMAF_integer_feature_motion2_v2_score`.
- **`integer_motion_hip`** — raw-pixel ping-pong (`pix[2]`) and the shared
  diff-first SAD kernel of `motion_v2_hip` (`integer_motion_sad_hip.c`), so the
  SAD is the CPU `motion`'s: `sum |blur(prev - cur)|`, rounded after each pass
  (ADR-1377). Host-side `motion2` / `motion3` and the debug `motion` score go
  through the CPU's `motion_fps_weight` / `motion_max_val` clip. Emits
  `VMAF_integer_feature_motion2_score` + `VMAF_integer_feature_motion3_score`.
- **`integer_psnr_hvs_hip`** — frequency-weighted distortion per 8×8 block,
  porting the CUDA twin and ADR-1369 native upload design. Uploads raw native
  samples via `vmaf_hip_picture_upload()` and converts on the device, eliminating
  host float conversions and unused pinned staging allocations. Emits `psnr_hvs`
  and per-channel variants. Its scores are the CPU extractor's bit for bit
  ([ADR-1401](../../adr/1401-psnr-hvs-sycl-hip-exact-twins.md)): the kernel
  stores the 64 masked coefficient errors of every block and the host adds them
  in the CPU's order, which costs a readback of 256 bytes per block (65 MB per
  3840x2160 frame); see
  [the psnr_hvs page](../../metrics/psnr-hvs.md#agreement-with-the-cpu-extractor).
  Takes the CPU extractor's `enable_chroma` option; with `enable_chroma=false`
  or 4:0:0 input only the luma plane is uploaded and scored.
- **`integer_ssim_hip`** — the CPU `ssim` extractor's algorithm, ported from
  the CUDA twin (`ssim_cuda.c`): a 9-tap integer Gaussian, int64 moments, the
  window truncated at the frame border, and the per-pixel SSIM term in double.
  Emits `ssim`. See [integer_ssim_hip](#integer_ssim_hip).
- **`integer_ms_ssim_hip`** — multi-scale SSIM over 5 pyramid levels; 9-tap
  biorthogonal LPF decimation + separable 11-tap Gaussian per scale. Emits
  `float_ms_ssim`, bit-identical to the CPU extractor. See
  [integer_ms_ssim_hip](#integer_ms_ssim_hip).
- **`integer_adm_hip`** — full ADM DWT2 + CSF + CM + decouple pipeline (five
  kernel files). Mirrors `integer_adm_cuda.c`. Emits `adm2` + per-scale values.
- **`integer_vif_hip`** — multi-scale VIF integer pyramid; respects
  `vif_skip_scale0` (PR #1063) and `vif_enhn_gain_limit`. Emits `vif_scale0..3`,
  bit-identical to the CPU extractor (ADR-1435). See
  [`vif_hip` returns the CPU's scores bit for bit](#vif_hip-returns-the-cpus-scores-bit-for-bit-2026-10-01).
- **`integer_cambi_hip`** — CAMBI banding detection; full HIP port per PR #996
  (ADR-0345 Phase 3). Emits `cambi`.
- **`ssimulacra2_hip`** — runs the whole frame on the device (ADR-1390, the
  HIP port of the SYCL chain of ADR-1363): one upload of the raw Y/U/V planes,
  one 864-byte readback of per-scale sums. Bit-identical to the CPU
  extractor since ADR-1445 (the terms in double precision, the sums with the
  result of the CPU's loops), at 2.8 to 2.9 times the frame time; see
  [ssimulacra2](../../metrics/ssimulacra2.md#hip-device-resident-tiled-row-pass).
  Emits `ssimulacra2`.
- **`float_adm_hip`** — ADM float pipeline, ninth kernel-template consumer
  (ADR-0468). Mirrors `float_adm_cuda.c`. Emits `float_adm2`.
- **`float_vif_hip`** — multi-scale VIF float pipeline; bit-identical to the
  CPU `float_vif` (see
  [float_vif_hip returns the CPU's scores](#float_vif_hip-returns-the-cpus-scores-bit-for-bit-2026-10-02)).
  Emits `float_vif_scale0..3`.

## Remaining stubs

`adm_hip`, `vif_hip`, and `motion_hip` use the older `_init/_run/_destroy` API
shape that requires a separate `VmafFeatureExtractor` redesign before promotion.
Each returns `-ENOSYS` at `init()`. Tracked in
[docs/state.md](../../state.md).

## Caveats

- `enable_hip` is `boolean` defaulting to **false**. `enable_hipcc` (also
  `boolean`, default **false**) controls whether `hipcc`-compiled kernel objects
  are linked. Both must be `true` for real GPU computation.
- HIP runtime types (`hipDevice_t`, `hipStream_t`) cross the public ABI as
  `uintptr_t`. This keeps `libvmaf_hip.h` free of `<hip/hip_runtime.h>`,
  mirroring the pattern Vulkan adopted in ADR-0184.
- No CI runner with a real AMD GPU exists on GitHub-hosted infrastructure.
  The CI compile lane (`Ubuntu HIP`) runs with `-Denable_hip=true`
  but `-Denable_hipcc=false`, so kernels are not compiled or exercised on CI.

## References

- [ADR-0212](../../adr/0212-hip-backend-scaffold.md) — the original scaffold.
- [ADR-0241](../../adr/0241-hip-first-consumer-psnr.md) — first consumer (`integer_psnr_hip`).
- [ADR-0254](../../adr/0254-hip-second-consumer-float-psnr.md) —
  second consumer (`float_psnr_hip`).
- [ADR-0259](../../adr/0259-hip-third-consumer-ciede.md) — third consumer.
- [ADR-0260](../../adr/0260-hip-fourth-consumer-float-moment.md) —
  fourth consumer (`float_moment_hip`).
- [ADR-0266](../../adr/0266-hip-fifth-consumer-float-ansnr.md) —
  fifth consumer (`float_ansnr_hip`), retained for historical
  traceability. The kernel and its CPU twin were removed in
  [ADR-0709](../../adr/0709-vmafx-phase4b-distributed-platform.md)
  (PR #38) — ANSNR is no longer a registered feature on any backend.
- [ADR-0267](../../adr/0267-hip-sixth-consumer-motion-v2.md) —
  sixth consumer (`motion_v2_hip`).
- [ADR-0372](../../adr/0372-hip-batch1-integer-psnr-float-ansnr.md) — batch-1 kernels.
- [ADR-0373](../../adr/0373-hip-batch2-float-motion.md) — batch-2 kernels.
- [ADR-0375](../../adr/0375-hip-batch3-float-moment-float-ssim.md) — batch-3 kernels.
- [ADR-0377](../../adr/0377-hip-batch4-ciede-motion-v2.md) — batch-4 kernels.
- `docs/adr/0379-hip-float-vif.md` — unavailable historical reference
  for `float_vif_hip`; [ADR-0592](../../adr/0592-hip-float-vif-stub-removal.md)
  records the later removal of its weak stub after the real kernel shipped.
- [ADR-0380](../../adr/0380-ffmpeg-patches-hip-backend-selector.md) — FFmpeg selector.
- [ADR-0468](../../adr/0468-hip-float-adm-real-kernel.md) — `float_adm_hip`.
- [ADR-0523](../../adr/0523-hip-integer-motion-extractor-registration.md) —
  register `vmaf_fex_integer_motion_hip`.
- [ADR-0533](../../adr/0533-hip-all-extractors-registration-sweep.md) —
  full HIP-extractor registration sweep (six more TUs wired into
  `hip_sources` + `feature_extractor_list[]`).
- [Research-0432](../../research/0432-hip-applicability.md) —
  AMD market-share + ROCm Linux maturity survey.

## ADR-0537: integer_vif_hip kernel fix (2026-05-18)

The integer VIF HIP extractor now runs end-to-end on AMD gfx1036 inside
the `vmaf-dev-mcp` container:

```bash
docker exec vmaf-dev-mcp vmaf \
    --reference /workspace/python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted /workspace/python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --backend hip --feature vif_hip --json --output /tmp/vif_hip.json
```

Reports the four VIF scale scores within `places=3` of CPU on the Netflix
golden pair.  The `places=4` parity target is tracked as an ADR-0537
follow-up — the residual ~0.001–0.003 per-scale delta comes from the
kernel's edge-clamp boundary vs CPU's pre-padded mirror boundary
(cumulative across downsamples).

Four defects fixed (see [ADR-0537](../../adr/0537-hip-integer-vif-kernel-fix.md)):

1. The 4×18 `vif_filter1d_table` is uploaded to a device buffer at init
   (the pre-fix kernel was handed a host pointer that the GPU faulted on).
2. Filter half-widths corrected from `{9,5,3,0}` (parsed from the kernel-
   name suffix — the wrong number) to `{8,4,2,1}` (= `vif_filter1d_width
   [scale] / 2`).  Pre-fix read 19/11/7/1 coefficients per output pixel
   from an 18-entry table.
3. Added the rd-filter downsample-write path so scales 1–3 read the half-
   resolution planes the previous horizontal pass produced.  Pre-fix left
   them uninitialised.
4. Picture buffers are staged into device memory via `hipMemcpy2DAsync`
   before scale-0 reads them (mirrors the `integer_motion_hip.c` pattern).

Adjacent fixes bundled in the same PR:

- Missing HSACO entries (`motion_score`, `ms_ssim_score`, `psnr_hvs_score`,
  `integer_ssim_score`, `float_vif_score`, `ssimulacra2_blur`,
  `ssimulacra2_mul`) added to `hip_kernel_sources` — ADR-0533 wired the
  extractor registration sweep but not the corresponding kernel compilation.
- Weak-stub TU `hip_hsaco_stubs.c` provides empty fallback `_hsaco`
  symbols for the four ADM kernels (`adm_dwt2`, `adm_csf`, `adm_csf_den`,
  `adm_cm`) that don't yet build standalone via `hipcc --genco` because
  they reference CUDA-specific helper macros.  As individual kernels
  port to standalone-buildable `.hip` sources, their weak-stub line is
  deleted from `hip_hsaco_stubs.c` in the same PR — ADR-0539 establishes
  that pattern, starting with `float_vif_score_hsaco` whose real kernel
  has shipped at `core/src/feature/hip/float_vif/float_vif_score.hip`
  since ADR-0379 / PR #1025.
- `hipcc --genco` include path adds `meson.current_build_dir()` +
  `feature/hip` + `hip` so kernel sources can resolve `config.h` /
  `integer_*_hip.h` headers.

Re-enables `VMAF_FEATURE_EXTRACTOR_HIP` on `vmaf_fex_integer_vif_hip` —
ADR-0530 had cleared it pending this fix.

## ADR-0539 — `integer_moment` HIP kernel registration (2026-05-18)

Closes the last unresolved-symbol gap in the
`enable_hipcc=true` HIP build.  Adds a new entry to
`hip_kernel_sources`:

```meson
'integer_moment_score' : feature_src_dir + 'hip/integer_moment/moment_score.hip',
```

The key is **distinct** from the pre-existing `moment_score` key (which
points at `hip/float_moment/moment_score.hip`).  The two keys emit
different `_hsaco` symbols (`integer_moment_score_hsaco` vs
`moment_score_hsaco`) consumed by `integer_moment_hip.c` and
`float_moment_hip.c` respectively.

End-to-end verification on the Netflix `src01_hrc00 ↔ src01_hrc01`
576×324 pair (HIP vs CPU, `--backend hip|cpu --feature
psnr|psnr_hvs|float_moment`):

| Feature              | HIP                     | CPU                     | delta |
|----------------------|-------------------------|-------------------------|-------|
| `psnr_y` mean        | 30.755064               | 30.755064               | 0.000000 |
| `psnr_cb` mean       | 38.449441               | 38.449441               | 0.000000 |
| `psnr_cr` mean       | 40.991910               | 40.991910               | 0.000000 |
| `psnr_hvs` mean      | 31.330446               | 31.330446               | 0.000000 |
| `psnr_hvs_y` mean    | 30.578766               | 30.578766               | 0.000000 |
| `psnr_hvs_cb` mean   | 37.258498               | 37.258498               | 0.000000 |
| `psnr_hvs_cr` mean   | 38.200260               | 38.200260               | 0.000000 |
| `float_moment_ref1st` mean | 59.788567         | 59.788567               | 0.000000 |
| `float_moment_dis1st` mean | 61.332007         | 61.332007               | 0.000000 |
| `float_moment_ref2nd` mean | 4696.668388       | 4696.668388             | 0.000000 |
| `float_moment_dis2nd` mean | 4798.659574       | 4798.659574             | 0.000000 |

All within places=4 of CPU (in fact bit-exact: delta=0.000000).

After this PR no weak HSACO stubs back any of the three integer-domain
PSNR / PSNR-HVS / moment extractors — only the four ADM kernels remain
on the ADR-0536 stub path pending their own CUDA-helper-macro port.

## Floating-point arithmetic of the kernels (ADR-1407)

Every HIP kernel is compiled with one flag list, `hip_strict_fp_args` in
`core/src/meson.build`:

| Flag | What it does |
|---|---|
| `-ffp-contract=off` | hipcc contracts `a * b + c` into one fused multiply-add for device code by default. The CPU reference build does not, so a contracted kernel rounds differently from the extractor it mirrors. |
| `-fhip-fp32-correctly-rounded-divide-sqrt` | Correctly rounded fp32 `/` and `sqrtf()`. This is hipcc's default; it is passed explicitly so that a toolchain default cannot move a score. |

With the list, fp32 and fp64 `+ - * /` and `sqrt` in a kernel round as on the
CPU. Scores are still not bit-identical where a twin uses a transcendental
function, sums in another order, or computes in fp32 what the CPU computes in
fp64; the per-twin numbers are in
[ADR-1407](../../adr/1407-hip-strict-fp-every-kernel.md).

There is no per-kernel flag table: a kernel listed in `hip_kernel_sources`
gets the policy. Until ADR-1407 only `ssimulacra2_blur`, `integer_ssim_score`
and `speed_pipeline` turned contraction off, through `hip_cu_extra_flags`
([ADR-0594](../../adr/0594-hip-ssimulacra2-blur-fp-contract-off.md)).

Two tests guard it. `test_hip_strict_fp_policy.py` checks the build files and
needs no device. `test_hip_fp_arith_contract` compiles a probe kernel with the
same list and compares a million random `a * b + c`, `a / b` and `sqrtf(a)`
results from the device with correctly rounded host values:

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip test_hip_fp_arith_contract test_hip_strict_fp_policy
```

## ADR-0539: integer ADM HIP kernels — real implementation (2026-05-18)

The four ADM kernels (`adm_dwt2`, `adm_csf`, `adm_csf_den`, `adm_cm`)
that the ADR-0537 sub-bundle had left as weak HSACO fallbacks now build
standalone via `hipcc --genco` and are registered in `hip_kernel_sources`.
The xxd-embedded strong symbols replace the weak slots in
`hip_hsaco_stubs.c` (which is now ADM-stub-free).

End-to-end on AMD gfx1036 inside `vmaf-dev-mcp`:

```bash
docker exec vmaf-dev-mcp vmaf \
    --reference /workspace/python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted /workspace/python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --backend hip --feature adm --json --output /tmp/adm_hip.json
```

Bit-exact vs CPU on the Netflix golden src01 pair (delta = 0.000000):

| Feature | CPU | HIP | Diff |
|---|---|---|---|
| `integer_adm`        | 0.934506 | 0.934506 | 0.000000 |
| `integer_adm2`       | 0.934506 | 0.934506 | 0.000000 |
| `integer_adm3`       | 0.953973 | 0.953973 | 0.000000 |
| `integer_adm_scale0` | 0.907897 | 0.907897 | 0.000000 |
| `integer_adm_scale1` | 0.893864 | 0.893864 | 0.000000 |
| `integer_adm_scale2` | 0.929998 | 0.929998 | 0.000000 |
| `integer_adm_scale3` | 0.964951 | 0.964951 | 0.000000 |

The CUDA twin's per-warp `__shfl_down_sync` reduction
(`cuda_helper.cuh::warp_reduce`) is replaced by per-thread `atomicAdd` on
the 64-bit unsigned accumulator. AMD wavefronts are 64 wide (not the 32
the CUDA shuffle mask hard-codes); per-thread atomicAdd is **bit-exact**
since uint64 addition is associative and commutative. Same pattern
`vif_statistics.hip` adopted (ADR-0537).

See [ADR-0539](../../adr/0539-hip-adm-kernels-real.md).

## ADR-1103: integer_vif_hip boundary fix — places=4 parity achieved (2026-06-13)

After the ADR-0563 carry-bit fix, `integer_vif_hip` still produced a residual
parity gap of places~2.75 (max |HIP−CPU| ≈ 0.0018 per scale) on the Netflix
src01 576×324 pair. The root cause was a boundary-condition mismatch: all
filter-loop reads used `clamp_i` (replicate-edge), while the CPU reference uses
a **symmetric reflect** (`PADDING_SQ_DATA` in `integer_vif.h`) and the CUDA twin
uses a "two-bounce mirror" in its shared-memory load stage.

The fix replaces `clamp_i` with `mirror2_i` in all six filter-loop reads in
`vif_statistics.hip`. Verification on gfx1030 (RDNA2, wave32):

| Scale | Max |HIP−CPU| (post-fix) | Places |
|-------|------------------------|--------|
| scale0 | 0.0000010 | ~6.00 |
| scale1 | 0.0000010 | ~6.00 |
| scale2 | 0.0000010 | ~6.00 |
| scale3 | 0.0000010 | ~6.00 |

See [ADR-1103](../../adr/1103-hip-vif-mirror2-boundary.md).

## Architectural limitations

### Zero-copy DMA-BUF / external memory import

Unlike the CUDA backend (which supports `cudaImportExternalMemory` via
`vmaf_cuda_picture_alloc`) and hardware decoders with DMA-BUF zero-copy pipelines,
the HIP backend currently does not provide zero-copy picture buffer import
(`VMAF_PICTURE_BUFFER_TYPE_HIP_DEVICE`).

Incoming frames arrive with `VMAF_PICTURE_BUFFER_TYPE_HOST` in system memory.
The planes the extractors of a run read are copied to the device once per
frame; see [Picture uploads](#picture-uploads).
Supporting direct DMA-BUF external memory import on AMD ROCm requires ROCm
`hipImportExternalMemory` plumbing and device picture pool support (T7-10c),
which is tracked as a deferred enhancement.

### Picture uploads

A frame's planes are uploaded once, whatever the number of extractors
([ADR-1408](../../adr/1408-hip-shared-frame-planes.md)). The first HIP
extractor of a frame that needs a plane uploads it into a buffer the
`VmafContext` owns, together with the other planes the extractors read in the
frame before; every other extractor reads that device copy. A plane no
extractor needs is not uploaded, so a luma-only run (the default model)
uploads no chroma. Thirteen extractors read the shared planes: `psnr_hip`,
`float_psnr_hip`, `float_moment_hip`, `ciede_hip`, `integer_ssim_hip`,
`float_ssim_hip`, `vif_hip`, `float_vif_hip`, `adm_hip`, `float_adm_hip`,
`motion_hip`, `motion_v2_hip` and `float_motion_hip`. With all of them in one
process a 4:2:0 frame pair used to be uploaded as 31 planes; it is now 6.

The upload does not return until the copy has finished reading the picture
(`vmaf_hip_picture_upload()`, `core/src/hip/picture_hip.h`). The pictures are
pageable host memory that the caller refills as soon as
`vmaf_read_pictures()` returns, and `hipMemcpy2DAsync` alone can still be
reading at that point. Pictures are read only while their frame is being
submitted, never afterwards.

What this means for a run:

- Scores are reproducible: every extractor gives the same per-frame output on
  every run and agrees with the CPU within its parity tolerance, and sharing
  the planes changes no output bit (thirteen extractors, both shipped models,
  576x324 to 3840x2160, 8 and 10 bits, with and without `--subsample`).
  Longer runs on a gfx1036 also show rare wrong frames that have nothing to
  do with uploads; see
  [Known issue: the gfx1036 loses stream commands](#known-issue-the-gfx1036-loses-stream-commands).
- The host waits for the device when planes are uploaded, which is once per
  frame from the second frame on, instead of once per extractor.
- Throughput on a gfx1036, ms per frame before and after sharing:
  `--model version=vmaf_float_v0.6.1` 57.3 to 46.9 at 1920x1080 (17.5 to
  21.3 frames per second) and 294 to 226 at 3840x2160, which gives back what
  the per-extractor wait had cost that model; `--model version=vmaf_v0.6.1`
  37.4 and 37.4 at 1080p; thirteen extractors in one process 183 and 184 at
  1080p, because their kernels are nearly all of the time; `motion_hip` and
  `motion_v2_hip` on their own 12.4 to 11.0 at 4K.
- `--subsample` is safe: an extractor that skips frames keeps the planes of
  the last frame it read until its kernels have finished, and the next upload
  into those buffers waits for the device first.

Extractors that convert or pack their input on the host still stage it
themselves: `float_ms_ssim_hip` (to `float`), `psnr_hvs_hip`, `cambi_hip`,
`speed_chroma_hip`, `speed_temporal_hip` and `ssimulacra2_hip`. They copy the
picture into pinned memory they own before `submit()` returns
(`vmaf_hip_picture_upload_staged()` for CAMBI and SpEED,
[ADR-1378](../../adr/1378-hip-cambi-device-resident.md),
[ADR-1384](../../adr/1384-hip-speed-device-resident.md)) and the device copy
runs from that buffer without a wait.
`T-HIP-SHARED-FRAME-REMAINING-TWINS-2026-10-01` in
[`docs/state.md`](../../state.md) tracks moving them onto the shared planes.

Three upload strategies were measured on the gfx1036 for the shared planes
([Research-1408](../../research/1408-hip-shared-frame-planes.md)): the waiting
upload, a host copy into pinned memory that the kernels read in place, and a
host copy into pinned staging followed by a device copy. The waiting upload
was the fastest or tied in every configuration, because this iGPU's runtime
copies a pageable picture without a host copy. A discrete AMD GPU is
unmeasured.

To check a build on your own hardware, run one extractor twice and compare:

```bash
for i in 1 2; do
  vmaf --reference ref.yuv --distorted dist.yuv \
       --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
       --backend hip --feature float_psnr_hip --no_prediction \
       --json --precision max --output run$i.json
done
cmp run1.json run2.json
```

Identical files do not prove the scores are right: with several extractors in
one process the old defect was deterministic. Compare against
`--backend cpu --feature float_psnr` as well, or run:

```bash
python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- \
  -C build test_hip_upload_race
```

That test also runs every extractor in one context, where the planes are
shared, with and without `n_subsample`, and refills both pictures the moment
a frame has been submitted. `test_hip_shared_frame` and
`test_hip_shared_frame_contract` check the sharing rules without a device.

### A frame clears its accumulators after its upload (ADR-1427)

Who this concerns: a program that creates more than one `VmafContext` with
HIP extractors in one process, for example to score a small clip and then a
larger one. The `vmaf` tool creates one context per process and was not
affected.

Before 2026-10-01 `float_moment_hip`, `vif_hip` and `adm_hip` returned a
wrong first frame in the first context of a process that needed larger
planes than the contexts before it. They cleared their accumulators ahead of
the frame's upload, and on a gfx1036 such a clear has no effect in that
situation: the frame's sums were added onto the sums the earlier context had
left in recycled device memory
([ADR-1427](../../adr/1427-hip-clear-after-upload.md)). Every HIP extractor
now uploads, then clears, then launches its kernels.

One frame in a 640x360 context, then one in a 3840x2160 context of the same
process, on `ryzen-4090-arc` (gfx1036, ROCm 7.2.4):

| First frame of the 3840x2160 context | Before | After | CPU |
|---|---|---|---|
| `float_moment_hip`, `float_moment_ref1st` | 130.53 | 127.00 | 127.00 |
| `vif_hip`, scale 0 | 0.6748 | 0.6934 | 0.6934 |
| `vif_hip`, scale 2 | 0.8749 | 0.8988 | 0.8988 |
| `adm_hip` | the run fails | identical | - |

The other eleven extractors of the test were correct before and are now.
Scores of later frames, of the first context of a process and of the `vmaf`
tool do not change. Re-run stored `float_moment_hip`, `vif_hip` or `adm_hip`
scores only if they came from a process that scored clips of rising size
through the library.

Time per frame is unchanged within the spread of the samples (medians of
three interleaved runs, `vif_hip` at 1080p of ten, other lanes loading the
host):

| Extractor | 1920x1080 before | after | 3840x2160 before | after |
|---|---|---|---|---|
| `float_moment_hip` | 1.95 | 1.97 | 7.22 | 7.14 |
| `vif_hip` | 46.86 | 47.84 | 207.30 | 202.10 |
| `float_psnr_hip` | 0.92 | 0.93 | 3.71 | 3.85 |

To check a device:

```bash
python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- \
  -C build test_hip_first_frame_clear_vif_hip \
  test_hip_first_frame_clear_float_moment_hip test_hip_first_frame_clear_adm_hip
```

There is one such test per extractor, each in its own process, because only
the first larger context of a process is exposed.
`test_hip_clear_after_upload_contract` checks the order in every HIP source
without a device.

### Dispatch strategy predicates and environment overrides

Runtime feature dispatch support can be probed via
`vmaf_hip_dispatch_supports(ctx, feature)`. Callers can customize feature
dispatch or disable specific HIP kernels via the `VMAF_HIP_DISPATCH`
environment variable:

```bash
# Disable specific HIP feature extractor, forcing CPU fallback
export VMAF_HIP_DISPATCH="float_ssim:disable,ciede:none"
```

## `integer_adm_hip` and the default model's ADM (2026-09-05)

The default model `vmaf_v1.0.16_3d0h` requests
`VMAF_integer_feature_adm3_score` with `adm_csf_mode=2`,
`adm_dlm_weight=0.7`, `adm_enhn_gain_limit=1.0`, `adm_min_val=0.5` and
`adm_noise_weight=0.02`, under the key
`integer_adm3_csf_2_dlmw_0.7_egl_1_min_0.5_nw_0.02`.

`integer_adm_hip` now honours `adm_csf_mode` (all four CSF models) and
`adm_p_norm`, and its `VmafOption` table is an entry-for-entry mirror of the
CPU table, so the `adm2` and `integer_adm_scale*` keys it emits are identical
to the CPU twin's for any options dict.

**`adm3_score` / `aim_score` are not emitted by this twin**, for the same
reason as the SYCL twin: no AIM device pass (the CUDA twin's ADR-0746
kernels). Both features are left out of `provided_features[]` so the ADR-0530
name-based fallback routes them to the CPU `integer_adm` twin. Tracked as
`T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05` in
[`state.md`](../../state.md).

Two CPU-parity corrections landed with the option work: `adm_min_val` no
longer clamps `adm2` (the CPU floors the adm3 expression only), and the
`numden_limit` precision floor scales with the full-frame area rather than the
scale-3 area.

### `adm_hip` returns the CPU's values bit for bit (2026-10-01)

`--backend hip --feature adm_hip` gives the same `integer_adm2` and
`integer_adm_scale0..3` as `--backend cpu --feature adm`, to the last bit, and
with `debug=true` the same per-scale numerators and denominators
([ADR-1423](../../adr/1423-hip-adm-cpu-row-rounding.md)). Integer ADM is
integer arithmetic up to the conclusion of a scale, so the twin can have the
CPU's accumulators; it now takes its CSF weights, its border, its rounding
shifts and the conclusion from the CPU extractor's own routines, and rounds
the denominator once per row as the CPU does.

Measured on a gfx1036 at `--precision max`, 21 fixture pairs (the Netflix
576x324 pair at 8, 10, 12 and 16 bits and as 4:2:2, both 1080p checkerboard
pairs, a flat pair, sizes down to 18x22, synthetic noise, stripes, impulses
and blocks, and 200 frames of BBB 3840x2160):

| | Before | After |
|---|---|---|
| Pairs identical to the CPU | 19 of 21 | 21 of 21 (6192 values with `debug=true`) |
| Gradient against impulses, 576x324 | `integer_adm_scale3` 4.0e-7 off | identical |
| BBB 3840x2160 | `integer_adm_scale0` up to 1.4e-7 off | identical |
| A 962x13542 frame | `integer_adm_scale0` 0.860 (CPU 0.979) | 0.979 |
| First frame of a context after a smaller one in the same process | garbage, the run fails | identical |

The last two rows were defects on unusual input, not rounding. At 962x13542
the device computed a rounding shift with an fp32 logarithm that is off by one
for 81 region sizes, and the host concluded with the CPU's shift. And each
frame cleared the accumulators ahead of its upload, where on this device the
clear is lost in the first context of a process that needs larger planes than
the contexts before it; the clear now follows the upload. The `vmaf` tool
creates one context per process and did not show the second defect; a program
that scores several clips through the library did.

The options keep their meaning: `adm_csf_mode` 1 to 3, the default model's
option set, `adm_enhn_gain_limit`, `adm_skip_scale0`, `adm_norm_view_dist` /
`adm_ref_display_height` and `adm_noise_weight` / `adm_p_norm` are identical
to the CPU on the same fixtures. A frame takes 19.1 ms at 1920x1080 and about
80 ms at 3840x2160 on the gfx1036, as before (77.4 and 80.4 in seven
interleaved runs whose samples overlap).

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip test_hip_adm_exact test_hip_adm_exact_contract
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-hip/tools/vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --backends cpu hip --features adm
```

### `vif_hip` returns the CPU's scores bit for bit (2026-10-01)

`--backend hip --feature vif_hip` gives the same `integer_vif_scale0..3` as
`--backend cpu --feature vif`, to the last bit, and with `debug=true` the same
frame ratio and per-scale numerator and denominator sums
([ADR-1435](../../adr/1435-hip-vif-cpu-log2-table.md)). The fixed-point VIF
statistic is integer arithmetic up to its last step and takes every per-pixel
logarithm from a table of 32768 entries that the CPU extractor fills with the
host math library. The twin used to evaluate `log2f()` on the device instead,
which is one ulp from glibc's for about half of the arguments and rounded
ties the other way; 77 entries came out one lower. It now uploads the CPU's
table at `init()` (64 KB) and the kernels look every logarithm up.

Measured on a gfx1036 at `--precision max`, frames whose score equals the
CPU's on scale 0 / 1 / 2 / 3:

| Fixture | Frames | Before | Max abs diff before | After |
|---|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 4 / 0 / 1 / 1 | 5.4e-7 | 48 / 48 / 48 / 48 |
| Checkerboard 1 px, 1920x1080 | 3 | 2 / 3 / 2 / 3 | 3.0e-8 | 3 / 3 / 3 / 3 |
| Checkerboard 10 px, 1920x1080 | 3 | 3 / 3 / 3 / 3 | 0 | 3 / 3 / 3 / 3 |
| Netflix 576x324, 10 bit | 3 | 1 / 0 / 0 / 0 | 3.6e-7 | 3 / 3 / 3 / 3 |
| Sparks 480x270, 10 bit | 5 | 0 / 0 / 3 / 1 | 3.6e-7 | 5 / 5 / 5 / 5 |
| BBB 3840x2160 | 48 | 5 / 3 / 5 / 3 | 3.0e-7 | 48 / 48 / 48 / 48 |

49 of 440 scores before, 440 of 440 after. After the change the fifteen
outputs of `debug=true` are identical too on those fixtures and on the
Netflix pair at 12 and 16 bits and as 10-bit 4:2:2 (2460 values), and the four
scores with `vif_enhn_gain_limit=1.0` and with `vif_skip_scale0=true` on the
same eight small fixtures (464 values each).

A frame takes no longer than before. Steady state inside one process, 21
interleaved pairs of runs while other lanes loaded the host (load average 10
to 95): 44.4 ms before and 42.7 ms after at 1920x1080, 188.2 and 163.4 ms at
3840x2160 (medians; the samples range from 32.3 to 52.8 and 32.4 to 49.1 ms,
and from 137.5 to 212.4 and 134.3 to 194.6 ms). A lookup replaces each
`log2f()`, and a pixel in the low-variance branch no longer computes the fp64
gain it does not use.

Stored `vif_hip` scores change by up to 5.4e-7; re-run them if you compare
against the CPU at full precision.

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip \
    test_hip_vif_parity test_hip_vif_parity_large test_hip_vif_log2_table_contract
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-hip/tools/vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --backends cpu hip --features vif
```

### `float_moment_hip` returns the CPU's moments bit for bit (2026-10-02)

`--backend hip --feature float_moment_hip` gives the same four
`float_moment_*` values as `--backend cpu --feature float_moment`, to the
last bit ([ADR-1447](../../adr/1447-hip-float-moment-cpu-float-squares.md)).
The CPU forms each sample's square in `float` before adding it. Up to 12 bits
per sample that is the exact square; at 16 bits it is the square rounded to
24 bits. The twin added exact squares, so its second moments
(`float_moment_ref2nd`, `float_moment_dis2nd`) were off at 16 bits. It now
adds the same rounded square as the CPU.

Measured on a gfx1036 at `--precision max`, frames whose second moments equal
the CPU's:

| Fixture | Frames | Before | Max abs diff before | After |
|---|---|---|---|---|
| Typical content at 8 and 10 bit (Netflix 576x324, 1080p checkerboards, Sparks, BBB 3840x2160) | 110 | 110 | 0 | 110 |
| Netflix 576x324 at 12 and 16 bit and as 10-bit 4:2:2; noise at 8, 10, 12 bit | 63 | 63 | 0 | 63 |
| Full-range noise 576x324, 16 bit | 3 | 0 | 2.8e-5 | 3 |
| Bright 16 bit, 1920x1080 | 2 | 0 | 1.0e-4 | 2 |
| BBB 1920x1080 as 16 bit | 40 | 0 | 7.5e-5 | 40 |
| BBB 3840x2160 as 16 bit | 32 | 0 | 3.9e-5 | 32 |

The first moments were identical before and are now. The repository's 16-bit
Netflix fixture is 8-bit content shifted left, which is why it never showed
the difference. If you stored 16-bit `float_moment_hip` second moments,
re-run them.

One range is not bit-identical. The CPU adds the squares into a `double`,
which holds the sum exactly up to 2^53 in units of 2^-16. A frame of up to
2 097 152 pixels (1920x1080 has 2 073 600) cannot reach that, and neither can
any frame at 8, 10 or 12 bits. A larger 16-bit frame whose second moment
times its pixel count reaches 2^37 does: from there the CPU's sum rounds as
it goes, and the twin, which adds exactly, can differ from it by at most
`(pixels - 2^21 + 1) / pixels * 2^(e - 69) + 2^-37` (`e` is 53 or 54 up to
3840x2160; 2.3e-5 at 3840x2160 with every sample near the peak). Measured:
2.7e-7 on a 2560x1440 frame with a tenth of its samples below 4096, 1.2e-7 on
full-range 3840x2160 noise, 0 on the 17 frames of the 16-bit BBB 3840x2160
fixture that are in that range (`T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02`).

The change costs nothing measurable: 1.94 and 2.02 ms per 16-bit 1920x1080
frame before and after, 11.1 and 10.6 ms per 16-bit 3840x2160 frame (medians
of 11 interleaved pairs; the samples overlap).

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip \
    test_hip_float_moment_parity test_hip_float_moment_parity_large \
    test_hip_float_moment_exact_contract
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-hip/tools/vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --backends cpu hip --features float_moment
```

### `ciede_hip` follows the CPU's arithmetic (2026-10-02)

`ciede.c` computes in double precision and stores in single, and adds every
pixel's colour difference into one `double` in raster order. `ciede_hip`
computed in single precision with another form of the formula and added per
wave and per 16x16 block: it matched the CPU on no frame and was up to 1.1e-5
from it. Since [ADR-1448](../../adr/1448-hip-ciede-cpu-arithmetic.md) the
kernel runs the CPU's statements with every `double` as a pair of `float`
values and every math function as a routine on such pairs. This is the
arithmetic of the SYCL twin, from the same header
(`core/src/feature/ciede_ff_math.h`). The kernel stores one `float` per
pixel, and the host adds them in the CPU's order.

The gfx1036 has `double`, and a first version ran the CUDA twin's
double-precision statements. Its math functions made a 1920x1080 frame take
318 ms instead of 18 ms, so that version was not merged.

Measured on a gfx1036 (ROCm 7.2.4, glibc 2.44) at `--precision max` against
`--backend cpu`:

| Fixture | Frames | Identical before | Max abs diff before | Identical after | Max abs diff after |
|---|---|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 0 | 1.1e-5 | 47 | 6.9e-13 |
| Checkerboard 1 px and 10 px, 1920x1080 | 6 | 0 | 8.6e-7 | 6 | 0 |
| Netflix 576x324, 10, 12 and 16 bit | 9 | 0 | 9.4e-6 | 9 | 0 |
| Netflix 576x324, 10-bit 4:2:2 | 48 | 0 | 1.1e-5 | 46 | 2.9e-12 |
| Sparks 480x270, 10 bit | 5 | 0 | 1.1e-6 | 4 | 2.4e-12 |
| BBB 3840x2160 | 48 | 0 | 1.4e-6 | 0 | 1.4e-11 |
| Full-range noise 576x324 at 8, 10, 12, 16 bit | 12 | 0 | 2.3e-7 | 3 | 4.6e-12 |
| Bright 16 bit, 1920x1080 | 2 | 0 | 1.6e-6 | 0 | 1.8e-12 |

The twin is not bit-identical, for two measured reasons. Of 437 million
pixels compared one by one with a host replay of the CPU's statements, 2 214
differ. 2 206 of them differ by one `float` step because glibc's `powf` is not
correctly rounded where the kernel's value is. The other 8, all in the first
twelve BBB frames, differ by one to nine steps because a pair holds 48 bits
where a `double` holds 53. The parity gate bounds the cell at `1e-9`
([cross-backend gate](../../development/cross-backend-gate.md)).

The pair arithmetic costs time (ms per frame, steady state, medians of three
interleaved pairs of runs):

| Frame | Before | After | |
|---|---|---|---|
| 1920x1080 | 18.6 | 49.6 | 2.7x |
| 3840x2160 | 75.6 | 210.1 | 2.8x |

At 1920x1080 the two L\*a\*b\* conversions take 21.9 ms and the colour
difference 25.5 ms; uploading the planes, launching the kernel, reading one
`float` per pixel back and the host's sum take 2.2 ms. At 3840x2160 the three
are 86.8, 113.5 and 9.8 ms. The CPU extractor takes 136 ms per 3840x2160 frame
on sixteen threads, so on this integrated GPU the twin is slower than the CPU
at that size (`T-HIP-CIEDE-EXACT-THROUGHPUT-2026-10-02`). The twin also keeps
one `float` per pixel on the device and on the host, 33 MB each at 3840x2160.

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip test_hip_ciede_parity test_hip_ciede_math
python3 scripts/dev/speed_gpu_parity.py --backend hip --feature ciede \
    --max-abs-diff 1e-9 --vmaf "$PWD/build-hip/tools/vmaf"
```

### `float_vif_hip` returns the CPU's scores bit for bit (2026-10-02)

`--backend hip --feature float_vif_hip` gives the same `vif_scale0..3` as
`--backend cpu --feature float_vif`, to the last bit, and with `debug=true`
the same frame ratio and per-scale numerator and denominator sums
([ADR-1444](../../adr/1444-hip-float-vif-cpu-arithmetic.md)). The twin runs
the arithmetic of the CUDA twin from one shared header
(`core/src/feature/float_vif_gpu_common.h`): the Gaussian taps the CPU
computes with `vif_get_filter()`, the CPU's polynomial `log2`, the noise
variance as a `double`, and one `float` sum per row and then over the rows.
Before, the kernel held a table of taps the CPU no longer uses, called the
device `log2f()`, took the noise variance as a `float` and added per wave and
per 16x16 block.

Measured on a gfx1036 at `--precision max`, frames whose score equals the
CPU's on scale 0 / 1 / 2 / 3:

| Fixture | Frames | Before | Max abs diff before | After |
|---|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 0 / 0 / 0 / 0 | 3.8e-5 | 48 / 48 / 48 / 48 |
| Checkerboard 1 px, 1920x1080 | 3 | 0 / 0 / 0 / 0 | 1.05e-6 | 3 / 3 / 3 / 3 |
| Checkerboard 10 px, 1920x1080 | 3 | 1 / 3 / 3 / 3 | 1.1e-12 | 3 / 3 / 3 / 3 |
| Netflix 576x324, 10 bit | 3 | 0 / 0 / 0 / 0 | 1.07e-5 | 3 / 3 / 3 / 3 |
| Sparks 480x270, 10 bit | 5 | 0 / 0 / 0 / 0 | 3.4e-6 | 5 / 5 / 5 / 5 |
| BBB 3840x2160 | 48 | 0 / 0 / 0 / 0 | 7.0e-6 | 48 / 48 / 48 / 48 |
| Netflix 576x324, 12 and 16 bit | 3 each | 0 / 0 / 0 / 0 | 1.07e-5 | all |
| Netflix 576x324, 10-bit 4:2:2 | 48 | 0 / 0 / 0 / 0 | 3.8e-5 | 48 / 48 / 48 / 48 |
| Full-range noise 576x324 at 8, 10, 12, 16 bit | 3 each | 0 / 0 / 0 / 0 | 1.0e-8 | all |
| Bright 16 bit, 1920x1080 | 2 | 0 / 0 / 0 / 0 | 1.06e-4 | 2 / 2 / 2 / 2 |

10 of 712 scores before, 712 of 712 after. The 1.06e-4 of the bright 16-bit
pair was above the 5e-5 the parity gate allowed the twin. With `debug=true`,
`vif_enhn_gain_limit=1.0` with `vif_sigma_nsq=1.5`, `vif_sigma_nsq=4.7`,
`vif_skip_scale0` and the per-scale floors (`vif_scale1_min_val`,
`vif_scale3_min_val`, new on this twin) the outputs are identical too on 62
frames of six of those fixtures (1922 values).

The old kernel also read in front of its buffer on a plane smaller than a
16x16 tile, and the gfx1036 faults on that read: `float_vif_hip` on a 64x64,
56x56 or 40x40 frame ended with `Memory access fault by GPU node-1` on three
of three runs each. Frames from 16x16 now run and return the CPU's bits
(16x16, 17x33, 71x20 checked).

A frame takes longer. Steady state inside one process, 11 interleaved pairs
of runs, host load average 3 to 13:

| Frame | Before | After | |
|---|---|---|---|
| 1920x1080 | 20.7 ms | 26.0 ms | +26 % |
| 3840x2160 | 86.0 ms | 147.1 ms | +71 % |

Where the increase goes, measured by taking one property out of the new twin
at a time: evaluating the two quotients in `double` costs 4.4 ms at 1920x1080
and 8.3 ms at 3840x2160; the plane of per-pixel terms that the row sums read
(two floats per pixel, 66 MB at 3840x2160) costs 1.1 ms and 37.8 ms; adding
each row in one thread costs 0.6 ms at 1920x1080 and nothing measurable at
3840x2160. On this integrated GPU the twin is slower than the CPU extractor
(46 ms per 3840x2160 frame on 16 threads). Tuning is tracked as
`T-HIP-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-02`; the scores must stay
bit-identical.

Stored `float_vif_hip` scores change by up to 3.8e-5 on typical content;
re-run them if you compare against the CPU.

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip \
    test_hip_float_vif_parity test_hip_float_vif_parity_large \
    test_hip_float_vif_exact_contract test_float_vif_device_math
python3 scripts/dev/speed_gpu_parity.py --backend hip \
    --vmaf "$PWD/build-hip/tools/vmaf" --feature float_vif
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-hip/tools/vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --backends cpu hip --features float_vif
```

## SpEED-chroma reports singularity separately from failure (ADR-1202, 2026-09-06)

The HIP SpEED-chroma twin previously treated any non-zero return from its
linear-algebra helper as "singular covariance matrix" and imputed the `uv`
score from the other chroma channel. That rule is correct for the CPU
extractor, where a non-zero return does mean singular, but not here: this twin
handles singularity internally (warn, zero the solution, return 0) and uses
the return value for API errors only. A real device error was therefore fed
into the imputation, and with both chroma channels failing it averaged to
`0.0` and reported success.

Singularity now travels in its own `bool *singular_out` and hard errors
propagate, so a device failure inside SpEED-chroma fails the frame instead of
emitting a `0.0` score. The twin also adopts the CPU rule that a channel with
exactly one singular side (reference or distorted) scores 0 rather than an
inflated value. The launch-geometry half of ADR-1202 was CUDA-only — this
twin's solve launch was already correct.

## Which HIP twins return the CPU's bits (2026-10-02)

A HIP twin is either an exact twin of its CPU extractor, with the same score
to the last bit, or it is within a tolerance of it. This table is the state on
`origin/master` a55b5fe07, measured on a gfx1036 (ROCm 7.2.4, glibc 2.44) at
`--precision max` against `--backend cpu`: 110 frames of typical content
(the Netflix 576x324 pair at 8 and 10 bits, both 1920x1080 checkerboard
pairs, Sparks 480x270 at 10 bits, 48 frames of BBB 3840x2160) and 68 frames
that stress the arithmetic (12 and 16 bits, 10-bit 4:2:2, full-range noise at
four depths, a bright 16-bit 1080p pair). "Values identical" counts every
output of every one of the 178 frames. On 2026-10-01 (`80c5a0332`) eight of
these twins were exact; "Before" is the largest difference measured then,
and [Research-1437](../../research/1437-hip-twin-exactness-sweep.md) has that
sweep per output and per fixture.

| CPU feature | HIP twin | Values identical | Largest difference | Before | Exact twin |
|---|---|---|---|---|---|
| `motion` (also `debug=true`) | `motion_hip` | 534 of 534 (712 of 712) | 0 | 0 | yes ([ADR-1437](../../adr/1437-hip-exact-twins-declared.md)) |
| `motion_v2` | `motion_v2_hip` | 534 of 534 | 0 | 0 | yes (ADR-1437) |
| `psnr` | `psnr_hip` | 534 of 534 | 0 | 0 | yes (ADR-1437) |
| `float_ms_ssim` (also `enable_lcs`) | `integer_ms_ssim_hip` | 178 of 178 (2848 of 2848) | 0 | 0 | yes (ADR-1437) |
| `cambi` | `cambi_hip` | 178 of 178 | 0 | 0 | yes (ADR-1437) |
| `adm` | `adm_hip` | 890 of 890 | 0 | 0 | yes ([ADR-1423](../../adr/1423-hip-adm-cpu-row-rounding.md)) |
| `float_motion` | `float_motion_hip` | 534 of 534 | 0 | 0 | yes ([ADR-1419](../../adr/1419-hip-float-motion-cpu-float-sum.md)) |
| `psnr_hvs` | `psnr_hvs_hip` | 680 of 680 (8 to 12 bits) | 0 | 0 | yes ([ADR-1401](../../adr/1401-psnr-hvs-sycl-hip-exact-twins.md)) |
| `vif` | `vif_hip` | 712 of 712 | 0 | 5.4e-7 | yes ([ADR-1435](../../adr/1435-hip-vif-cpu-log2-table.md)) |
| `ssim` | `integer_ssim_hip` | 178 of 178 | 0 | 1.1e-11 | yes ([ADR-1438](../../adr/1438-hip-ssim-cpu-frame-sum.md)) |
| `float_psnr` | `float_psnr_hip` | 178 of 178 | 0 | 7.6e-8 dB | yes ([ADR-1440](../../adr/1440-hip-float-psnr-exact-block-sums.md)) |
| `float_ssim` (also `enable_lcs`) | `float_ssim_hip` | 178 of 178 (712 of 712) | 0 | 5.4e-7 | yes ([ADR-1441](../../adr/1441-hip-float-ssim-cpu-window-sums.md)) |
| `float_vif` | `float_vif_hip` | 712 of 712 | 0 | 1.1e-4 | yes ([ADR-1444](../../adr/1444-hip-float-vif-cpu-arithmetic.md)) |
| `ssimulacra2` | `ssimulacra2_hip` | 178 of 178 | 0 | 7.6e-11 | yes ([ADR-1445](../../adr/1445-hip-ssimulacra2-cpu-sum-order.md)) |
| `float_moment` | `float_moment_hip` | 712 of 712 | 0 | 1.0e-4 | yes, while the CPU's own sum is exact ([ADR-1447](../../adr/1447-hip-float-moment-cpu-float-squares.md)) |
| `speed_chroma` | `speed_chroma_hip` | 528 of 534 | 1.4e-6 | not measured | no (the C library's `log2f`) |
| `float_adm` | `float_adm_hip` | 224 of 1246 | 1.3e-5 | 1.3e-5 | no |
| `ciede` | `ciede_hip` | 115 of 178 | 1.4e-11 | 1.1e-5 | no (the C library's `powf`, and the last bits of a pair; [ADR-1448](../../adr/1448-hip-ciede-cpu-arithmetic.md)) |

The parity gate compares an exact twin with tolerance 0
([cross-backend gate](../../development/cross-backend-gate.md),
[the list](../../development/cross-backend-exact-twins.md)); the others keep
their tolerance:

- `float_moment_hip` is exact on every frame of up to 2 097 152 pixels and on
  every 8-, 10- and 12-bit frame. On a larger 16-bit frame whose sum of
  squares passes 2^53 the CPU's own sum rounds as it goes and the twin is
  within a derived bound of it
  (see [float_moment_hip](#float_moment_hip-returns-the-cpus-moments-bit-for-bit-2026-10-02)).
- `speed_chroma_hip`: 6 values differ, by at most 1.4e-6 (Netflix 8-bit
  frame 3, BBB frames 17 and 21). The cause is on the CPU side: `speed.c`
  calls glibc's `log2f`, which is not correctly rounded, and the device
  rounds `log2` correctly. The same CPU binary with a correctly rounded
  `log2f` preloaded returns the twin's bits on every value of those two
  fixtures, also over all 200 BBB frames (600 values, 11 of which differ
  from the plain CPU run). These are the values and the cause of the CUDA
  twin ([ADR-1430](../../adr/1430-cuda-speed-chroma-log2f-bound.md)). The
  gate compares the HIP cell at `5e-6`, the CUDA cell's bound
  ([ADR-1452](../../adr/1452-hip-speed-chroma-log2f-bound.md),
  `T-HIP-SPEED-CHROMA-GLIBC-LOG2F-2026-10-02`).
- `float_adm_hip` has not been ported to the CPU's arithmetic
  (`T-HIP-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01`); the CPU reference itself
  changed on 2026-10-02 (the decouple step divides).
- `ciede_hip` runs the CPU's arithmetic on pairs of `float` values since
  ADR-1448 and is within 1.4e-11; the gate bounds its cell at `1e-9`
  (see [ciede_hip](#ciede_hip-follows-the-cpus-arithmetic-2026-10-02)).

The exact twins were not all free. Frame time on the gfx1036 before and after
each twin became exact, from its own pull request (ms per 1920x1080 frame and
per 3840x2160 frame): `vif` 44.4 to 42.7 and 188.2 to 163.4; `ssim` 28.2 to
30.0 and 94.3 to 98.1; `float_psnr` 0.96 to 0.99 and 3.97 to 3.98;
`float_ssim` 1.72 to 2.02 and 4.86 to 5.18 (17.7 to 23.4 and 82.3 to 109.6
with `scale=1`); `float_vif` 20.7 to 26.0 and 86.0 to 147.1; `ssimulacra2`
58.1 to 167.0 and 233.7 to 662.4; `float_moment` unchanged. The open tuning
rows are `T-HIP-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-01`,
`T-HIP-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-02` and
`T-HIP-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.

`float_ms_ssim` is exact up to one rounding: the per-scale means are rounded
to `float` on both sides, which absorbs the order in which the twin adds the
windows unless a sum lies within its own rounding error of a rounding
boundary. No mean differed on these frames.

```bash
python3 scripts/ci/run_meson_test.py -- -C build-hip test_hip_exact_twins
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-hip/tools/vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --backends cpu hip \
    --features vif motion motion_debug motion_v2 adm psnr float_moment ssim \
        float_ssim float_ssim_lcs float_ms_ssim float_ms_ssim_lcs float_psnr \
        float_motion float_vif psnr_hvs ssimulacra2 cambi
```

## RC3 CPU parity: motion, tiny frames and CPU options (2026-09-30)

Three changes bring HIP twins onto the CPU's arithmetic. They build for
gfx90a, gfx1030, gfx1036 and gfx1100 and pass every device-free check, and on
2026-10-01 they passed on a gfx1036 (Ryzen 9950X3D iGPU, ROCm 7.2.4): every
HIP device test OK, `motion_hip` equal to the CPU `motion` on every frame, and
the parity gate within its tolerances (numbers under
[Measured on a gfx1036](#measured-on-a-gfx1036-2026-10-01)). The commands
below repeat the check on another AMD device.

**`motion_hip` is expected to match the CPU `motion` exactly**
([ADR-1377](../../adr/1377-hip-motion-diff-first.md)). The CPU differences the
two frames, blurs the difference, and rounds after the vertical and after the
horizontal pass. `motion_hip` used to blur each frame and difference the
blurred frames, which rounds differently (the 1.26e-5 at the top of this
page). It now runs the kernel `motion_v2_hip` already used
(`integer_motion_v2/motion_v2_score.hip`, launched only by
`integer_motion_sad_hip.c`), and its debug `motion` score carries
`motion_fps_weight` and `motion_max_val` like the CPU's. Both motion twins
read the frame's luma from the shared planes and keep the previous frame in a
device plane of their own (see [Picture uploads](#picture-uploads)).

**Small frames stay inside the device buffers**
([ADR-1381](../../adr/1381-hip-integer-tiny-frame-guards.md)). A tiled kernel
loads a whole tile for every thread, padding threads included, and reflects an
out-of-plane index once; on a plane smaller than the tile that reflection can
still land outside it. The motion and float-motion tile loads and the integer
ADM scale-0 vertical DWT now clamp the reflected index into the plane
(`hip_tile_index.h`, `integer_adm/adm_dwt2_rows.h`). The clamp is the identity
for every sample an output reads, so no score changes;
`test_hip_adm_dwt2_rows` replays every thread of the launch for every plane
height up to 8192 on the host. `vif_hip` now needs 16x16 frames, as `vif_sycl`
does: its filters reflect once per scale and need 16 pixels at scale 3. With a
model, smaller frames run on the CPU `vif`; `--feature vif_hip` below 16x16
fails at init.

**Four twins take the CPU options**
([ADR-1382](../../adr/1382-hip-twin-cpu-option-parity.md)):

| Twin | Options added | How |
|---|---|---|
| `psnr_hip` | `enable_mse`, `enable_apsnr`, `reduced_hbd_peak`, `min_sse` | Host, from the device SSE, through the CPU's `psnr_score.h`; `apsnr_*` in `flush()` |
| `integer_ssim_hip` | `enable_db`, `clip_db` | Host, `vmaf_ssim_max_db()` and the shared SSIM emitter |
| `float_ssim_hip` | `enable_lcs`, `enable_db`, `clip_db` | `enable_lcs` selects a pass-2 kernel that also reduces the per-pixel L, C and S |
| `float_motion_hip` | `motion_max_val` (`mmxv`) | Host; every emitted score, the debug one included, goes through the CPU's `motion_clip()` |

With `enable_db`, identical frames report what the CPU reports. For
`integer_ssim_hip` that is `+inf` (or the `clip_db` ceiling) on frames above
4096 pixels. Smaller frames report the CPU's own value, which is usually `+inf`
and sometimes a finite value one or two ulps below a perfect score: 156.54 dB
on an identical 1x1 frame of zeros, 159.55 dB on a flat 3x3 frame of 51
([ADR-1400](../../adr/1400-hip-integer-ssim-raster-sum-small-frames.md)).
`float_ssim_hip` computes each pixel's term as the CPU does, `l * c * s` from
the CPU's own luminance, contrast and structure types, and rounds the frame
mean to fp32 like the CPU. On some identical frames the CPU's fp32 arithmetic
leaves 1 - 2^-24, which is 72.247 dB, and the twin reports the same instead
of a forced `+inf`.

Three more CPU behaviours: `motion_v2_hip` stores its SAD weighted by
`motion_fps_weight` and capped at `motion_max_val`, as the CPU does, and
emits `motion2_v2` / `motion3_v2` for a one-frame run; `psnr_hip` sees every
frame under `--subsample`, so the `apsnr_*` totals cover the clip; and
`motion_hip` defaults `debug` to false and writes
`VMAF_integer_feature_motion_sad_score` every frame, like the CPU `motion`.
With `motion_force_zero=true`, `motion_hip` and `float_motion_hip` write the
CPU's zero scores; before this change both crashed on the first frame.

### float_motion_hip options

`float_motion_hip` emits `motion3` and takes the whole CPU `float_motion`
option table
([ADR-1404](../../adr/1404-hip-float-motion-motion3-and-options.md)):

| Option | Where it runs |
|---|---|
| `motion_blend_factor` (`mbf`), `motion_blend_offset` (`mbo`) | Host: `motion3` is the CPU's blend of the fps-weighted score, then the `motion_max_val` cap |
| `motion_filter_size` (`mfs`) | Kernel argument: `3` selects the 3-tap filter, `1` no blur, anything else the 5-tap filter |
| `motion_add_scale1` (`mdc`) | A second kernel scales both blurred frames to half size with the CPU's bilinear scaler and stores their differences; the row kernel adds them |
| `motion_add_uv` (`mau`) | The same kernels run on the U and V planes and the three scores add up; 4:0:0 input is refused |

A frame is still one upload call, one read-back and one wait, whatever the
options.

`float_motion_hip` returns the CPU extractor's scores bit for bit, with every
option ([ADR-1419](../../adr/1419-hip-float-motion-cpu-float-sum.md)). The CPU
adds the absolute differences of a row into one `float`, the row sums into a
second one, and divides in `float`, and those running sums round at every
step, so the score depends on the order of the additions. The twin used to add
16x16 blocks and was 3e-6 (576x324) to 1.4e-4 (1080p checkerboards) from the
CPU. It now stores every absolute difference and adds each row left to right
on the device, one thread per row, and the rows on the host. The differences
are stored transposed, 64 rows to a group with a column's samples adjacent,
because the threads of a wave walk different rows: read from the blurred
planes, the row sums took 145 ms per 3840x2160 frame on the gfx1036.

Measured on a gfx1036 against `--backend cpu` at `--precision max`, `motion`,
`motion2` and `motion3` on the Netflix 576x324 pair at 8 bits (48 frames) and
10 bits (3), both 1080p checkerboard pairs (3 each) and BBB 3840x2160 (20
frames), 231 values per option set:

| Options | Identical before | Max abs diff before | Identical after |
|---|---|---|---|
| none | 10 | 1.36e-4 | 231 |
| `motion_add_scale1=true` | 10 | 2.21e-4 | 231 |
| `motion_add_uv=true` | 10 | 1.36e-4 | 231 |
| `motion_add_scale1=true:motion_add_uv=true` | 10 | 2.21e-4 | 231 |
| `motion_filter_size=3` | 10 | 1.58e-4 | 231 |
| `motion_filter_size=1` | 10 | 2.46e-5 | 231 |
| `motion_fps_weight=1.5:motion_blend_factor=0.5:motion_blend_offset=2:motion_max_val=4` | 177 | 2.62e-6 | 231 |

The 10 values that matched before are the zero scores of first frames, and
the 177 of the last row are mostly scores at the `motion_max_val` cap.

Time per frame on the gfx1036, medians of seven interleaved runs of both
builds with other jobs loading the host:

| Run | 1920x1080 before | 1920x1080 after | 3840x2160 before | 3840x2160 after |
|---|---|---|---|---|
| default options | 2.50 | 2.78 | 18.09 | 19.76 |
| `motion_add_scale1` | 3.03 | 3.74 | 21.02 | 24.68 |
| `motion_add_uv` | | | 19.05 | 20.52 |

`--model version=vmaf_float_v0.6.1`, of which `float_motion_hip` is one
extractor, reads 49.98 and 50.11 ms per 1920x1080 frame and 192.66 and 199.17
per 3840x2160 frame before and after (medians of five interleaved runs), which
is inside the spread of the runs.

To confirm on an AMD host, build with HIP in the `vmaf-dev-mcp` container and
run the device tests, which skip (exit 77) without a device:

```bash
meson setup build-hip core -Denable_hip=true -Denable_hipcc=true \
    -Dhip_gfx_targets=gfx1036
ninja -C build-hip
python3 scripts/ci/run_meson_test.py -- -C build-hip \
    test_hip_motion_tiny_frames test_hip_twin_option_parity \
    test_hip_vif_min_dim test_hip_adm_tiny_frames test_hip_upload_race \
    test_hip_float_motion_parity test_hip_float_motion_rows
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-hip/tools/vmaf \
    --reference testdata/ref_576x324_48f.yuv --distorted testdata/dis_576x324_48f.yuv \
    --width 576 --height 324 --backends cpu hip \
    --features float_ssim float_ssim_lcs psnr motion_v2 vif float_motion
```

Replace `gfx1036` with your device's target (`rocm_agent_enumerator` prints
it). The per-row commands, with the expected numbers and a timing run, are in
[`docs/state.md`](../../state.md) under `T-HIP-MOTION-BLUR-THEN-DIFF-2026-09-29`,
`T-CUDA-HIP-ADM-DWT-VERT-TINY-HEIGHT-OOB-2026-09-29`,
`T-GPU-INTEGER-VIF-MIN-DIM-TWINS-2026-09-29` and
`T-BUG048-GPU-OPTION-PARITY-REMAINDER-2026-09-26`.

### float_ssim_hip at 1080p and 4K

The CPU `float_ssim` decimates both planes before SSIM by
`max(1, round(min(w, h) / 256))`: 1 below 384 px, 4 at 1920x1080, 8 at
3840x2160; the `scale` option forces the factor. `float_ssim_hip` used to
implement scale 1 only, so at 1080p and 4K `--backend hip --feature float_ssim`
and models computed the feature on the CPU and printed
`float_ssim_hip cannot run 3840x2160 8-bit pictures with these options`.
Since [ADR-1405](../../adr/1405-hip-float-ssim-device-decimation.md) the twin
decimates on the device, with the CPU's reduced planes bit for bit, and runs
every size:

```bash
vmaf --reference ref.yuv --distorted dist.yuv \
     --width 3840 --height 2160 --pixel_format 420 --bitdepth 8 \
     --backend hip --feature float_ssim \
     --no_prediction --json --output ssim.json
# ssim.json: "feature_backends": [{"extractor": "float_ssim_hip", "backend": "hip"}]
```

The twin falls back to the CPU extractor only when the decimated plane is
smaller than the 11x11 SSIM window (for example 100x100 at `scale=10`) or the
scale is above 128; `--feature float_ssim_hip` fails at init in those cases.

Measured on a gfx1036 against `--backend cpu` at `--precision max` when
ADR-1405 landed:

| Clip | Scale | Max abs diff | Twin | CPU, 16 threads | Before (CPU fallback) |
|---|---|---|---|---|---|
| 3840x2160, 50 frames | 8 (auto) | 1.79e-6 | 5.5 ms/frame | 11.9 ms/frame | 19.7 ms/frame |
| 1920x1080, 24 frames | 4 (auto) | 4.23e-6 | 2.0 ms/frame | 2.7 ms/frame | 6.6 ms/frame |
| Netflix 576x324, 48 frames | 1 (auto) | 1.79e-7 | unchanged | — | — |
| Netflix 576x324 | 2, 3, 5, 10 | at most 1.79e-7 | — | — | — |

#### `float_ssim_hip` returns the CPU's score bit for bit (2026-10-01)

Those differences are gone
([ADR-1441](../../adr/1441-hip-float-ssim-cpu-window-sums.md)). The CPU adds
the eleven products of a Gaussian window in `double` and rounds once per
pass; the twin added them in single precision. It now forms both window
passes and the luminance, contrast and structure terms through the same
arithmetic as `integer_ms_ssim_hip`, which carries the `double` sum as an
exact pair of floats.

Measured on a gfx1036 at `--precision max`: 178 of 178 frames identical to
`--backend cpu` (27 before; the Netflix 576x324 pair at 8, 10, 12 and 16 bits
and as 10-bit 4:2:2, both 1920x1080 checkerboard pairs, Sparks 480x270 at 10
bits, 48 frames of BBB 3840x2160, full-range noise at four depths, a bright
16-bit 1080p pair), and all four outputs of `enable_lcs=true` on the same
frames (712 values). `scale=1`, `scale=3`, `enable_db` and `clip_db` are
identical too.

The exact sums cost time. Steady state, medians of 11 interleaved pairs of
runs:

| Run | Before | After | Change |
|---|---|---|---|
| 1920x1080, default scale (4) | 1.72 ms | 2.02 ms | +17 % |
| 3840x2160, default scale (8) | 4.86 ms | 5.18 ms | +7 % |
| 1920x1080, `enable_lcs=true` | 1.98 ms | 2.31 ms | +17 % |
| 1920x1080, `scale=1` | 17.7 ms | 23.4 ms | +32 % |
| 3840x2160, `scale=1` | 82.3 ms | 109.6 ms | +33 % |

Stored `float_ssim_hip` scores change by up to 4.8e-7.

### Measured on a gfx1036 (2026-10-01)

| Check | Result |
|---|---|
| HIP device tests (`--suite gpu`, 52 tests) | all OK; the only skip marker was `test_hip_float_ssim_parity_large`, because `float_ssim_hip` did not decimate yet (it does since ADR-1405, and the test runs) |
| `motion_hip` against `--backend cpu`, Netflix 576x324 pair | `motion2` / `motion3` identical (1.26e-5 apart before) |
| Parity gate, `float_ssim float_ssim_lcs psnr motion_v2 vif` | every cell OK: 1.0e-5, 1.0e-5, 0, 0, 1.0e-6 |
| `psnr_hip` with all four CPU options | every per-frame value and every `apsnr_*` identical to the CPU |
| `float_motion_hip` on 3x3 and 17x17 frames | exits 0, within 4e-6 of the CPU |
| BBB 4K, ms per frame (master / this change) | `motion_hip` 14.25 / 12.95, `motion_v2_hip` 10.17 / 13.24 |

`motion_v2_hip` got slower on this iGPU because of the staged upload, not
the kernel: with the waiting `vmaf_hip_picture_upload()` the same kernel
runs at 10.70 ms per frame, and `motion_hip` at 10.75. On an integrated GPU
the runtime copies a pageable picture without an extra host copy, so the
staged path's copy into pinned memory costs more than the wait it saves.
`T-HIP-UPLOAD-WAIT-THROUGHPUT-2026-09-19` in [`docs/state.md`](../../state.md)
has the numbers, including runs with several twins, where the two uploads are
within noise. Since ADR-1408 both motion twins read the shared planes through
the waiting upload again; see [Picture uploads](#picture-uploads).

### Known issue: the gfx1036 loses stream commands

On this gfx1036 (ROCm 7.2.4, Linux 7.2.8) a HIP stream now and then never
runs a run of the commands it was given, roughly once per 10^4 frames, on
master as well. A HIP twin then reports a wrong score for that frame:
`vif_hip`, for example, reports the sums of two frames when the memset of
its accumulators is lost, or fails the run with `invalid ratio` when a scale's
kernel is lost. Nothing in vmafx sets it off, and no runtime setting tried
stops it. To check a device or a driver update, run the probe that
reproduces it without vmafx:

```bash
hipcc -O2 --offload-arch=gfx1036 scripts/dev/hip_dispatch_drop_probe.hip -o /tmp/probe
for i in 1 2 3 4 5; do /tmp/probe 100000 12 0; done
```

Each run prints `bad_frames` and `lost` (dispatches that never ran); both
are 0 on a healthy stack. On the gfx1036 five runs gave 55 bad frames in
500000 and 382 lost dispatches in 6.0 million. Until a driver update clears
it, compare HIP scores from this device over repeated runs and treat a
single-frame mismatch as suspect, not as a code defect
(`T-HIP-GFX1036-DROPPED-DISPATCHES-2026-10-01`).

## RC3: CAMBI and SpEED run entirely on the device (2026-09-30)

`cambi_hip` ([ADR-1378](../../adr/1378-hip-cambi-device-resident.md)) and
`speed_chroma_hip` / `speed_temporal_hip`
([ADR-1384](../../adr/1384-hip-speed-device-resident.md)) no longer run any
stage of the CPU extractors on the host. Each frame is one staged upload (see
[Picture uploads](#picture-uploads)), the whole pipeline on the extractor's
stream, and one small read of the result; `collect()` is the only host wait.
They port the SYCL designs of ADR-1357 and ADR-1358 and build for gfx90a,
gfx1030, gfx1036 and gfx1100, but have not yet run on an AMD device.

What to expect from the scores:

- **`cambi_hip`** keeps `cambi.c`'s sliding column histograms and sums the
  top-K c-values exactly, so it scores bit-identically to `--backend cpu`
  wherever the CPU's own top-K sum is exact (every sub-4K frame measured on
  SYCL). It now refuses, as the CPU does, a window whose adjusted size exceeds
  65 x 65.
- **`speed_chroma_hip` / `speed_temporal_hip`** reproduce `speed.c` in fp32
  operation for operation. On HIP that takes build flags, not intrinsics: the
  kernel file is compiled with `-ffp-contract=off` and
  `-fhip-fp32-correctly-rounded-divide-sqrt`, because HIP's `__fmul_rn()`,
  `__fadd_rn()` and `__fdiv_rn()` are the plain (contracting) operators and
  `__fsqrt_rn()` is the approximate native square root. The scores equal the
  CPU's bit for bit when the CPU's `log2f` is correctly rounded; with a glibc
  (gcc) build a few `speed_chroma` frames differ in the last float bits. The
  [SpEED page](../../metrics/speed_qa.md#hip-device-resident-cpu-fp32-arithmetic)
  shows how to compare with a correctly rounded `log2f`.

Request the twins by name; `--feature cambi` or `--feature speed_chroma` runs
the CPU extractor whatever `--backend` says:

```bash
vmaf -r ref.yuv -d dis.yuv -w 576 -h 324 -p 420 -b 8 --no_prediction \
     --backend hip --feature cambi_hip --feature speed_chroma_hip \
     --feature speed_temporal_hip --json -o hip.json
```

Without a device, `test_hip_cambi_device_math` and `test_hip_speed_device_math`
replay every kernel on the host against the CPU extractors, and
`test_hip_device_resident_contract.py` checks the sources keep one upload, one
readback and one wait per frame. The on-device parity and timing commands are
in [`docs/state.md`](../../state.md) under
`T-HIP-CAMBI-HOST-RESIDUAL-2026-09-29` and
`T-HIP-SPEED-HOST-RESIDUAL-2026-09-29`.
