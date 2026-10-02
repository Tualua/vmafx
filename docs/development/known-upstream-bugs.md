# Known upstream bugs

Bugs that reproduce on `upstream/master` (Netflix/vmaf) as well as this fork's
`master`, discovered during fork work but out of scope for the PR that found
them. Each entry records the reproducer, the evidence it is upstream, and the
suggested fix.

When a fork-local PR touches the same file, prefer to fix the bug in that PR
and reference this entry in the commit. If the PR does not touch the file,
file a follow-up ticket and link to it here.

---

## Open pull requests this fork has sent upstream

Fifteen, all open on 2026-10-01. One (#1602) has drawn a review comment; none
has been approved or merged. No CI has ever run on any of them: every workflow
on the upstream repository sits at `action_required`, waiting for a maintainer
to approve a first-time contributor's run.

The right-hand column says whether the fork's own tree carries each fix,
checked on 2026-10-01 against the fork's code at master `591d53449`; the
evidence for each row is in [`docs/state.md`](../state.md) under "Confirmed
not-affected".

| Upstream PR | What it fixes | In the fork |
| --- | --- | --- |
| [#1588](https://github.com/Netflix/vmaf/pull/1588) | option dictionaries leak when an overload or a registration fails | Fixed and tested (`T-UPSTREAM-1242-FEATURE-DICT-OWNERSHIP-2026-09-03`) |
| [#1589](https://github.com/Netflix/vmaf/pull/1589) | median and percentile pooling on the C API | Present and tested (`T-UPSTREAM-818-POOLING-ENUM-NO-PERCENTILES-2026-09-03`) |
| [#1590](https://github.com/Netflix/vmaf/pull/1590) | a failed model-collection growth loses the collection | Fixed; test added by PR #1663 |
| [#1591](https://github.com/Netflix/vmaf/pull/1591) | thread-pool creation ignores `pthread_create()` errors | Fixed and tested; a partly started pool stays usable here |
| [#1599](https://github.com/Netflix/vmaf/pull/1599) | scale-3 DWT reads index -1 for frame dimensions 17 to 32 | Fixed and tested (`T-ADM-SCALE3-TINY-FRAME-OOB-READ-2026-09-18`) |
| [#1600](https://github.com/Netflix/vmaf/pull/1600) | `pow(2, shift - 1)` with a shift of 0 in `adm_cm` | Fixed and tested (`T-ADM-AVX512-SMALL-WIDTH-SCALE0-2026-09-18`) |
| [#1601](https://github.com/Netflix/vmaf/pull/1601) | signed overflow in the 16-bit vertical DWT, and a left shift of negative taps | Fixed and tested, by widening to int64 (`T-ADM-DWT2-16BIT-INT32-OVERFLOW-2026-09-18`) |
| [#1602](https://github.com/Netflix/vmaf/pull/1602) | `adm_cm` SIMD and scalar disagree above a coefficient of 15360 | Second revision taken in every implementation ([ADR-1402](../adr/1402-adm-cm-centre-tap-int32.md)); the fork differs from upstream master on such content until upstream merges it |
| [#1603](https://github.com/Netflix/vmaf/pull/1603) | checkasm passes wrong strides to the ADM DWT tests | Not affected: no `checkasm` tree |
| [#1604](https://github.com/Netflix/vmaf/pull/1604) | frames with an odd width or height lose framing; a reader error becomes a crash | Not affected; test added by PR #1664 |
| [#1606](https://github.com/Netflix/vmaf/pull/1606) | a zero-length variable-length array with `--no_prediction` | Not affected: no VLA (ADR-0809) |
| [#1620](https://github.com/Netflix/vmaf/pull/1620) | SpEED initialises on frames too small for one block | Fixed and tested |
| [#1621](https://github.com/Netflix/vmaf/pull/1621) | a bit-depth mismatch between reference and distorted is accepted | Fixed and tested (`test_validate_pic_params_bpc`) |
| [#1627](https://github.com/Netflix/vmaf/pull/1627) | `speed_temporal` overruns its buffers at `speed_prescale` above 1 | Ported, fork PR #1643 (`T-SPEED-TEMPORAL-PRESCALE-UP-OVERFLOW-2026-09-30`) |
| [#1629](https://github.com/Netflix/vmaf/pull/1629) | `cambi` walks outside frames shorter than its window | Ported, fork PR #1642 (`T-CAMBI-SHORT-FRAME-OOB-2026-09-30`) |

Three of these differ from what the fork carries, which matters at the next sync:

- **#1601 takes a cheaper fix than the fork's.** The fork widens the accumulator
  to int64 (PR #1477). Upstream measured that at 3.5 to 6 % of throughput, so
  the upstream patch starts the sum from the normalization offset instead, which
  adds no operation and measures within noise. Both are correct. **That
  approach is worth bringing back to the fork** as a performance change; it has
  not been done.
- **#1602 changed direction after review, and the fork followed.** Its first
  revision made SIMD follow the scalar int16 wrap of the masking centre tap.
  Its second revision (2026-09-21) removes the wrap from the scalar, x86 edge
  and CUDA code instead, after a reviewer there called the wrap wrong. Measured
  in the fork on 2026-10-01, the wrap is an artefact: a flat reference with
  isolated impairments scored `integer_adm_scale0` above 1 where `float_adm`
  gives exactly 1. The fork removed the wrap from the scalar, AVX2, AVX-512,
  CUDA, HIP, SYCL and Metal code the same day
  ([ADR-1402](../adr/1402-adm-cm-centre-tap-int32.md)), after checking that no
  Netflix golden assertion moves. Until upstream merges the pull request the
  fork's integer ADM differs from upstream master on content that reaches a
  centre coefficient of 15360, such as full-range noise; `docs/state.md` row
  `T-ADM-CM-CENTRE-TAP-WRAP-ABOVE-ONE-2026-10-01` has the measured deltas.
- **#1591 and #1588 are narrower than the fork.** The fork keeps a thread pool
  that started at least one worker, and its `vmaf_use_feature()` consumes the
  dictionary on a failed copy too. Keep both at a sync.

Upstream PR [#1494](https://github.com/Netflix/vmaf/pull/1494) (open since April,
by an upstream maintainer) refactors the same ADM functions. It does not touch
the lines above, but whichever lands first leaves the other needing a rebase.

## Upstream defects verified on `6ec23e8f2`, checked against the fork (2026-10-01)

Fifteen defects were reproduced on upstream master `6ec23e8f2` while answering
Netflix issues on 2026-10-01. Each was run against the fork's `master` with the
reproducer from the upstream report, on this host (RTX 4090, gfx1036, Arc
A380 under `xe`; GCC 16.2.1, Clang 22.1.8). Three reproduced and are fixed;
twelve do not. The reproducers are kept under
`~/.cache/vmafx-upstream-rebase/evidence/` on the host, one directory per issue.

| Upstream | Defect | On the fork |
| --- | --- | --- |
| [#1305](https://github.com/Netflix/vmaf/issues/1305) | Several CUDA instances in one process give wrong `motion2` and non-finite `vif` scales: an accumulator reset on the extractor stream, the kernels on the picture stream | **Reproduced in `integer_vif_cuda`, fixed** by PR #1750. Four instances on one context: wrong values in 15 of 15 runs before, none in 105 after. Every other CUDA reset (`motion_sad`, `adm`, `float_adm`, `float_psnr`, `psnr`, `cambi`, `kernel_template.h`) is on the kernels' stream; four instances of each of 19 CUDA extractors give identical output, and so do four HIP instances of the default model and twelve HIP extractors |
| [#1300](https://github.com/Netflix/vmaf/issues/1300) | Every init/close cycle leaks device memory and host memory (`cuModuleLoadData` without unload, streams not destroyed) | **Not affected.** Upstream's `repro1300`, 30 cycles of 1080p: device memory +0 MiB for the default model and for each of 19 extractors (upstream master +676 MiB), host memory flat after the first cycle (+1.2 MB over 29 cycles, upstream +763 MB); with `malloc_trim(0)` after every cycle about 4 KiB per cycle. HIP, 30 cycles: +0 MiB device after the first cycle, +80 KiB host over 29 cycles. [ADR-0157](../adr/0157-cuda-preallocation-leak-netflix-1300.md) and `test_cuda_module_lifecycle_contract.py` pin the unloads |
| [#1420](https://github.com/Netflix/vmaf/issues/1420) | `vmaf_cuda_buffer_alloc()` asserts on out-of-memory | **The allocator does not assert** (`CHECK_CUDA` returns `-ENOMEM`; `test_cuda_buffer_alloc_oom`). **A worse failure was found end to end and fixed** by PR #1752: after the allocation failed the CLI hung in `vmaf_close()` holding the device lock, because a failed `vmaf_read_pictures()` kept its pictures. Not reproducible on HIP: the iGPU allocates from system memory |
| [#1180](https://github.com/Netflix/vmaf/issues/1180), [#755](https://github.com/Netflix/vmaf/issues/755) | A per-frame score asked for before the flush fails or races | **By design, documented** by PR #1753. The call returns the value or `-EAGAIN`; with the multi-instance harness, queries made 2 and 3 frames behind the newest picture never returned a different number |
| [#910](https://github.com/Netflix/vmaf/issues/910) | `vmaf_read_pictures()` accepts a non-increasing or gapped index and scores wrong | **A repeated or earlier index is rejected** with `-EINVAL` ([ADR-0152](../adr/0152-vmaf-read-pictures-monotonic-index.md)). A gap is accepted and its effect on the motion scores is documented ([ADR-1429](../adr/1429-read-pictures-index-gaps-accepted.md), PR #1753); no wrong value is returned without an error |
| [#761](https://github.com/Netflix/vmaf/issues/761) | `--model path=C:\...` and `C:/...` split at the drive-letter colon | **Not affected.** `cli_split()` keeps a drive-letter colon in the value ([ADR-1190](../adr/1190-cli-option-string-escape-grammar.md), [ADR-1355](../adr/1355-cli-option-value-backslashes.md)); `vmaf -m 'path=C:\VMAF_evaluation\model\vmaf_v0.6.1.json'` and the `C:/` form reach the model loader as one path (`could not read model from path: "C:\VMAF_evaluation\..."` on Linux, where upstream says `bad option string`). `test_model_path_windows_drive_letter` pins it |
| [#1414](https://github.com/Netflix/vmaf/issues/1414) | `float_ms_ssim` below 176x176 fails late with a confusing message | **Not affected.** The CPU extractor refuses at `init()`: `float_ms_ssim: input resolution 176x144 is too small; the 5-level 11-tap MS-SSIM pyramid requires at least 176x176`, exit 234, no output file; 176x176 scores. The CUDA, HIP and SYCL twins say the same at 176x144 (exit 234) and score 176x176 (measured on all three). `test_float_ms_ssim_min_dim`. The twins differ in one place: the CPU, SYCL and Metal also refuse a chroma plane below 176 when `enable_chroma` is on, while CUDA and HIP accept the option and score luma only (open row `T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06`) |
| [#1568](https://github.com/Netflix/vmaf/issues/1568) | `vmaf_write_output()` opens a UTF-8 path with narrow `fopen` on Windows | **Not affected.** `vmaf_write_output()` opens through `vmaf_open_utf8()`, models and inputs through `vmaf_fopen_utf8()`; both convert UTF-8 to wide on Windows (`core/src/compat/path_utf8.c`, `test_path_utf8`). Read from the source: no Windows host was run. Two narrow `fopen` calls remain, in the vendored `pelorus_qp_report_csv.c` (fix in pelorus, then re-vendor) and in the `VIF_OPT_DEBUG_DUMP` dump, which writes a fixed ASCII path |
| [best15](https://github.com/Netflix/vmaf/pull/1605) | The AVX2 `get_best15_from32()` shifts by a negative count and calls `clz(0)` on lanes the blend discards | **Not affected.** `decouple_s123_best15_avx2()` calls it only for magnitudes of 32768 and above (`adm_avx2.c`). `vmaf --feature adm --cpumask 16` (AVX2 only) and the default dispatch on the Netflix pair and the 1080p checkerboard pair under ASan and UBSan: no runtime error; the `fast` suite of that build: 215 of 215 |
| aim-uninit | `score_aim` is read uninitialised when the ADM denominator is 0 (`adm_noise_weight=0`, flat reference) | **Not affected.** A flat reference with `adm_noise_weight=0` makes the frame fail with `integer_adm: undefined or non-finite aggregate at frame 0 (num=0 den=0 ...)` (and the float twin the same), the extractor returns the error before it reads the score, and no `aim` or `adm3` is emitted. Both ratios are written on every success path (`vmaf_adm_scale_ratios()`, `vmaf_adm_finalize_scores()`). Run under ASan and UBSan; not under MSan |
| [#1562](https://github.com/Netflix/vmaf/issues/1562) | The CUDA motion `mirror()` is off by one against the CPU | **Not affected.** Netflix pair at `--precision max`, `--backend cpu` against `--backend cuda`: `integer_motion2`, `integer_motion3` and `vmaf` identical on 48 of 48 frames at 8 bits and at 10 bits (ADR-1372: one shared kernel) |
| [#1606](https://github.com/Netflix/vmaf/pull/1606) (`master-bagging.log`) | A half-built model leaks when a collection file is read as a single model | **Not affected.** `vmaf --model version=vmaf_b_v0.6.3` and `--model path=model/vmaf_b_v0.6.3.json` exit 0 under ASan and UBSan with no LeakSanitizer report; the `path=` form logs a warning where upstream logs an error |
| [#1553](https://github.com/Netflix/vmaf/pull/1553), [#1583](https://github.com/Netflix/vmaf/pull/1583) | CUDA with `--threads N` (N >= 2) exits 234, `context could not be synchronized`: `motion_cuda` is flushed twice | **Not affected.** The default model on the Netflix pair, `--threads 0`, `2`, `4`, `8`: exit 0 on CUDA, HIP and SYCL (A380), every output identical across thread counts (672 values on CUDA and SYCL, 720 on HIP; pooled `vmaf` 82.816058734212 on CUDA) |
| [#1612](https://github.com/Netflix/vmaf/pull/1612) | `motion_cuda` reads its previous-blur buffer uninitialised on frame 0 and leaks 8 bytes per init/close | **Not affected.** `compute-sanitizer --tool initcheck` on 5 frames: 0 errors for `motion_cuda` and `float_motion_cuda`; `--leak-check full`, 3 init/close cycles: 0 bytes leaked for both |
| [#1613](https://github.com/Netflix/vmaf/pull/1613) | With CUDA device input a chroma plane never reaches the host picture (`psnr_cb` at the 60 dB cap) | **Reproduced, fixed** by PR #1754: the device-to-host copy took the luma plane only. A CUDA run on host input is not affected (`psnr` y/cb/cr equal the CPU at `--threads 0` and `4`; same on HIP, and on SYCL to 7e-15) |

The remaining Netflix change since `6ec23e8f2`, `8e7a1ac4e` ("integer_vif:
restore `void *` cursor in `vif_buffer_alloc`", 2026-10-01 18:05 UTC),
reverts `6ec23e8f2` (PR #1476), which broke the build with GCC 14 and later
and with Clang 22 ([#1630](https://github.com/Netflix/vmaf/issues/1630)). The
fork needs nothing from it: it never took #1476, and its
`vif_buffers_alloc()` in `core/src/feature/integer_vif.c` walks a byte cursor
with typed casts. `integer_vif.c` compiles with GCC 16.2.1 and with Clang
22.1.8 under `-Werror=incompatible-pointer-types`. That makes seven upstream
commits since the September port that the fork does not need, up to
`8e7a1ac4e`; `docs/state.md` ("Confirmed not-affected") lists the first six.

## Upstream head the fork is at parity with: `cea2b4d83` (2026-10-02)

Upstream master moved from `8e7a1ac4e` to `cea2b4d83` with
[Netflix/vmaf#1653](https://github.com/Netflix/vmaf/pull/1653), three commits
on SpEED:

| Upstream commit | What | On the fork |
| --- | --- | --- |
| `76ea5f03` | `speed`: the scalar anti-alias filter and the 16x decimation fused into `vif_filter1d_dec16_s()`, called on every target but x86 | **Ported.** Bit-identical to `vif_filter1d_s()` + `vif_dec16_s()` in `core/test/test_speed_filter.c` (GCC 16.1 and clang 22.1 for aarch64 under `qemu-aarch64`, GCC on x86); x86 reports are byte-identical before and after |
| `cea2b4d8` | checkasm case for the fused filter | **No checkasm tree here.** Its sizes and layouts are rows of `test_speed_filter.c` |
| `15297286` | `arm64`: NEON covariance kernel for SpEED | **Not ported: SIMD not bit-exact.** Eight partial sums with fused multiply-adds against the scalar kernel's one running sum: 4061 of 18480 sums differ in the last bits, by up to 3.5e-12 relative (upstream tests it to 1e-10). Not an upstream defect; the fork's arm64 kernels have to return the scalar's bits (`core/src/feature/arm64/AGENTS.md`). The commit's wider test matrix for the covariance kernels runs for AVX2 and AVX-512 in `core/test/test_speed_simd.c` |

`docs/rebase-notes.md` has the mechanics and the measurements. Upstream branch
`speed-fused-avx2` (not merged) moves x86 to the fused filter too.

## Reported upstream on 2026-09-19

Six defects found on `86da14d0` while validating the pull requests above were
reported on 2026-09-19, each reproduced on upstream first. The right-hand
column is this fork's own status, checked against the fork's tree the same day
rather than inferred from upstream's.

| Upstream | What | This fork |
| --- | --- | --- |
| [#1603](https://github.com/Netflix/vmaf/pull/1603) (PR) | checkasm's `check_adm_dwt2` passes a byte stride where `adm_dwt2_16()` indexes samples, and a second site passes a band stride as the source stride; ASan: heap over-read | **Not affected** — the fork carries no `checkasm` tree |
| [#1604](https://github.com/Netflix/vmaf/pull/1604) (PR) | The direct YUV and y4m readers read floor-sized chroma rows where the file stores ceil-sized ones, and `fetch_picture()` returns `!ret`, turning a reader error into "usable picture" and a crash | **Not affected** on both counts: `picture_compute_geometry()` allocates ceiling chroma, `USE_DIRECT_READ` is never defined so the buffered reader runs, the CLI refuses odd 4:2:0 dimensions outright, and `finish_unread_picture()` maps errors to `-1`. The one piece upstream left open — two failed reads classified as a clean end of stream, and every read failure exiting 0 — **was live here** and is fixed by ADR-1262 |
| [#1605](https://github.com/Netflix/vmaf/pull/1605) (PR) | AVX2 `get_best15_from32()` shifts by a negative count on every lane before the blend discards it | **Not affected** — the AVX2 helper has returned early below 32768 since PR #792; scalar, AVX-512, CUDA, HIP, Metal and SYCL guard at the call site |
| [#1606](https://github.com/Netflix/vmaf/pull/1606) (PR) | A zero-length variable-length array when `--no_prediction` leaves `model_cnt` at 0 | **Not affected** — `ModelArrays::allocate()` returns before allocating when the count is 0 (ADR-0809) |
| [#1607](https://github.com/Netflix/vmaf/issues/1607) (issue) | Frames of 16 px and below crash integer ADM: `(uint32_t)ceil(log2(w) - 4)` converts a negative double, and `h_half - 2` underflows an unsigned bound in `dwt2_src_indices_filt()`. Upstream #1599 and #1600 do not fix it | **Not affected** — the extractor refuses the input with `integer_adm requires width >= 17 and height >= 17` instead of running; measured at 8, 12 and 16 px. Whether to refuse or support such frames is the decision upstream was asked to make |
| [#1608](https://github.com/Netflix/vmaf/issues/1608) (issue) | The SIMD `adm_cm` narrows `accum_h` to `float` before dividing where its siblings and scalar do not | **Same cast present** (`adm_avx512.c`), **no effect**: the divisor is an exact power of two, all 108 values over the three reference pairs are bit-identical, and 2,000,000 random integers in `[2^53, 2^62)` show no difference |

The earlier note on the last item — that the cast "likely explains upstream's
1e-4 checkasm tolerance" — was wrong and is withdrawn: instrumenting the three
tolerance sites over a full `checkasm --test=adm` run gives 90 comparisons, 87
exactly equal, and a worst relative deviation of `1.910e-07`, roughly 500 times
inside the tolerance.

---

## Integer ADM vector decouple rounds the gain-limited sample — fixed in this fork

Found 2026-10-01; present on `upstream/master` `6ec23e8f2`; not reported
upstream.

The scalar decouple stores `MIN(rst * adm_enhn_gain_limit, t)` (or `MAX`), a
double, in an integer, which truncates toward zero. Upstream's vector kernels
convert the product with rounding conversions: `_mm256_cvtpd_epi32` in
`adm_decouple_avx2` (`adm_avx2.c:854`), `_mm512_cvtpd_epi32` in
`adm_decouple_avx512` (`adm_avx512.c:964`) and `_mm512_cvtpd_epi64` in
`adm_decouple_s123_avx512` (`adm_avx512.c:1407`). With an integral limit (the
default 100, the 1 of the NEG models) the product is integral and nothing
shows. With a non-integer limit the vector result is one off wherever the
product has a fraction of one half or more.

Reproduce on any x86 host:

```bash
vmaf -r src01_hrc00_576x324.yuv -d src01_hrc01_576x324.yuv -w 576 -h 324 -p 420 -b 8 \
     --feature 'adm=adm_enhn_gain_limit=1.2' --json -o simd.json
vmaf ... --feature 'adm=adm_enhn_gain_limit=1.2' --json -o scalar.json --cpumask 4294967295
```

`integer_adm_scale0` differs by up to 1.2e-6 per frame on that pair, 6.2e-6 on
the 352x288 `akiyo` pair and 3.5e-5 on blurred blocks.

**Fix applied in this fork:** the truncating conversions (`_mm256_cvttpd_epi32`,
`_mm512_cvttpd_epi32`, `_mm512_cvttpd_epi64`), so every dispatch level returns
the scalar's sample. See
[ADR-1413](../adr/1413-adm-gain-limit-truncated-double-product.md). Until
upstream changes its kernels, the fork's AVX2 / AVX-512 output under a
non-integer limit equals upstream's scalar output, not upstream's vector
output.

## `adm_decouple_s123_avx512` LTO+release SEGV — fixed in this fork

**Status:** fixed in this fork (PR #69 follow-up commit), still present upstream.

**Symptom:** `test_pic_preallocation` aborts with
`AddressSanitizer: SEGV on unknown address` inside
`adm_decouple_s123_avx512` when the binary is built with
`--buildtype=release -Db_lto=true -Db_sanitize=address`. The debug
ASan build used by CI (`--buildtype=debug -Db_lto=false`) does not
reproduce the crash.

Reproduce with:

```bash
meson setup build-asan-lto libvmaf \
  -Denable_cuda=false -Denable_sycl=false \
  -Db_sanitize=address --buildtype=release -Db_lto=true
ninja -C build-asan-lto test/test_pic_preallocation
ASAN_OPTIONS=detect_leaks=1 ./build-asan-lto/test/test_pic_preallocation
```

**Evidence it is upstream, not fork-local:** the same reproducer on
`origin/master` (no fork-local patches applied) produces the same
crash. The faulting instruction is
`vmovdqa64 zmm2, ZMMWORD PTR [rdi-0xc0]`, a 64-byte-aligned AVX-512
load served a 32-byte-aligned address.

**Why CI does not catch it:** CI's sanitizer job uses
`--buildtype=debug -Db_lto=false`, which keeps every
`_mm512_loadu_si512` as `vmovdqu64` (unaligned) and so runs fine. The
`--suite=unit` filter in `tests-and-quality-gates.yml` also matches
zero tests in `core/test/meson.build`, so the job reports green
even if the link succeeds. Tracked separately — the suite filter
needs to be corrected.

**Root cause:** the stack array `int64_t angle_flag[16]` inside
`adm_decouple_s123_avx512` is loaded via
`_mm512_loadu_si512(&angle_flag[0])` and
`_mm512_loadu_si512(&angle_flag[8])`. Under LTO, link-time
alignment inference promotes the unaligned loads to the aligned
`vmovdqa64` form. The C-level default stack alignment for an
`int64_t[16]` is 8 bytes, so the promoted aligned load faults on
every other 64-byte slot.

**Fix applied in this fork:** annotate the stack array with
`_Alignas(64)` at
[`core/src/feature/x86/adm_avx512.c:1317`](../../core/src/feature/x86/adm_avx512.c#L1317).
The unaligned load remains correct, and the LTO-promoted aligned
form is now also correct.

**Related issue surfaced during triage:**
`test_picture_pool_basic`, `test_picture_pool_small`, and
`test_picture_pool_yuv444` loaded a `VmafModel` via
`vmaf_model_load` and never called `vmaf_model_destroy`, so
LeakSanitizer reported 208 bytes direct + 23 KiB indirect leaks per
test. Pairing `vmaf_model_destroy(model)` with each load is also
landed in PR #69 (same commit).

---

## Integer AIM is not clipped at 1, float AIM is — kept as upstream has it

Found 2026-10-01; present on `upstream/master` `6ec23e8f2`; not reported
upstream.

The two ADM extractors finish the AIM ratio differently:

```c
/* libvmaf/src/feature/integer_adm.c:3006-3007 */
// normalize AIM score by the DLM denominator
*score_aim = aim_num / den;

/* libvmaf/src/feature/adm.c:322-323 */
// normalize AIM score by the DLM denominator and clip values larger than 1
*score_aim = MIN(aim_num / aim_den, 1.0f);
```

On a reference without detail the denominator is the noise floor alone, and
any visible additive impairment takes the ratio above 1. Upstream prints
`integer_aim` 3.175585 and `aim` 1.000000 for a flat grey 64x64 reference
against the same picture with isolated 4x2 patches; `adm3` follows
(`integer_adm3` 0.0, `adm3` 0.5 at the default weight).

It is not known which line upstream intends. The shipped `vmaf_v1.0.16` models
read the integer feature. **The fork keeps both lines as they are**
([ADR-1417](../adr/1417-integer-aim-unclipped-upstream-parity.md)) and
documents the two ranges in [features](../metrics/features.md#aim-above-1);
`test_integer_adm_aim_unclipped` pins both. If upstream adds the clip to the
integer extractor, port it and change that test in the same PR.

## `KBND_SYMMETRIC` single-reflection at sub-kernel-radius input sizes

**Status:** fixed in this fork (PR #69), still present upstream.

**Symptom:** For a 2-D convolution with a 9-tap kernel on inputs
smaller than the kernel half-width (n ≤ 3 for `LPF_HALF = 4`),
upstream's `KBND_SYMMETRIC` reflects the index only once; the
reflected index is still out of bounds, causing an out-of-bounds read.

Reproduce with:

```c
/* With upstream KBND_SYMMETRIC, idx=-4, n=1 reflects to 3 (OOB for n=1). */
float v = KBND_SYMMETRIC(img_1x1, 1, 1, -4, 0, 0.0f);  /* reads img[3] */
```

**Why it is latent upstream:** MS-SSIM pyramids never decimate below
~60×34 in practice, and SSIM / ADM similarly never feed a 1×1 input
through the convolver. Nothing in Netflix/vmaf's test corpus exercises
the regime.

**Fix applied in this fork:** `KBND_SYMMETRIC` and
`ms_ssim_decimate_mirror` (scalar + AVX2 + AVX-512 + NEON) are
rewritten in the period-based (`period = 2*n`) form that bounces
correctly for any offset. See
[`docs/adr/0125-ms-ssim-decimate-simd.md`](../adr/0125-ms-ssim-decimate-simd.md)
and the inline comment in
[`core/src/feature/iqa/convolve.c`](../../core/src/feature/iqa/convolve.c).
