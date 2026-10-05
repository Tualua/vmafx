<!-- markdownlint-disable MD013 MD060 -->
# Exact twins by bit depth and chroma layout

The [exact-twin table](cross-backend-exact-twins.md) lists the GPU twins that
return the CPU extractor's bits. This page records how far that claim was
measured: every exact twin of CUDA, SYCL and HIP at 8, 10, 12 and 16 bits and in
4:2:0, 4:2:2 and 4:4:4, on generated fixtures, compared with `--backend cpu` of
the same binary at `--precision max`.

| Mark | Meaning |
|---|---|
| `=` | Every output of every frame has the CPU's bits, and both runs have the same outputs. |
| `n/a` | The CPU extractor refuses the depth (`psnr_hvs` takes at most 12 bits). Allowed at 16 bits only. |
| `FAIL` | A value differs or one run lacks an output. |
| `ERROR` | A run failed (at 8, 10 or 12 bits, a CPU failure is an error too). |

There is no tolerance. A cell that differs is a defect with a row in
[the state ledger](../state.md), fixed or reported; it never gets a bound.

## The fixtures

`scripts/ci/exact_twin_matrix.py` writes the fixtures with integer arithmetic
only, so every host gets the same bytes. Each fixture is four frames of 357x353:

- The width is odd and neither 16 nor 64 divides it, so every row ends in a
  partial tile, and a pitched device plane is wider than its row. 4:2:0 and
  4:2:2 chroma rows are 179 samples (ceil-subsampled).
- At 353 rows, 4:2:0 chroma is still at least 176 on each side, so
  `float_ms_ssim` with `enable_chroma` runs in every layout.
- Each plane holds a ramp that moves 3 pixels per frame, blocks inverted per
  plane, hashed noise, and runs clipped to 0 and to the depth's maximum. The
  distorted file adds its own noise and moves one window by 5 pixels.

## Run it

```bash
python3 scripts/ci/exact_twin_matrix.py --vmaf-binary build/tools/vmaf --backends cuda
```

`--backends` takes `cuda`, `sycl` and `hip` together or one at a time. Use
`--depths`, `--layouts` and `--features` to run part of the matrix, and
`--md-out` / `--json-out` to keep the result. When `~/.cache/vmafx-locks`
exists (or `VMAFX_LOCK_DIR` names a directory), each device run takes that
backend's lock file, with a 300 s limit inside the lock. Exit status: 0 when
every cell is `=` or `n/a`, 1 when a cell failed, 77 when the CLI refuses the
backend (no device), and 2 on a usage error.

Meson registers one device test per backend the build enables:
`test_cuda_exact_twin_matrix`, `test_sycl_exact_twin_matrix` and
`test_hip_exact_twin_matrix` (suites `slow`, `gpu` and the backend name). Each
one runs the whole matrix of its backend and skips without a device. Load the
oneAPI environment before you run the SYCL test.

## Record a run

A twin declared exact (a fragment in `scripts/ci/exact_twins.d/`) needs a
recorded row here. After a full run of one backend, write its table into this
page and commit the result:

```bash
python3 scripts/ci/exact_twin_matrix.py --vmaf-binary build/tools/vmaf --backends cuda \
    --record docs/development/exact-twin-matrix.md \
    --recorded-on "GCC 16 release build of <commit> (CUDA sm_89), <date>"
```

`--record` replaces only the block of each backend it ran, adds the cell
counts to the `--recorded-on` line, and refuses a partial run.
`test_exact_twin_matrix_contract` (fast suite, no device) fails when an exact
twin of CUDA, SYCL or HIP has no recorded row, or a row with a cell other than
`=` (or `n/a` at 16 bits). Metal twins are measured by the
[macOS tester bundle](../usage/tester-image.md) and are not recorded here.

## Recorded results

### CUDA

<!-- exact-twin-matrix:cuda:begin -->

GCC 16 release build of `9bc68a108` (no LTO, CUDA sm_89 and HIP gfx1036 kernels), 2026-10-05: 285 of 288 cells equal, 3 `n/a`.

| backend | feature | 8/420 | 8/422 | 8/444 | 10/420 | 10/422 | 10/444 | 12/420 | 12/422 | 12/444 | 16/420 | 16/422 | 16/444 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cuda | `adm` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `cambi` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_adm` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_moment` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_motion` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_ms_ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_ms_ssim_chroma` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_ms_ssim_lcs` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_psnr` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_ssim_lcs` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `float_vif` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `motion` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `motion_debug` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `motion_mffw` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `motion_v2` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `motion_v2_mffw` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `psnr` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `psnr_hvs` | = | = | = | = | = | = | = | = | = | n/a | n/a | n/a |
| cuda | `speed_chroma` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `speed_temporal` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `ssimulacra2` | = | = | = | = | = | = | = | = | = | = | = | = |
| cuda | `vif` | = | = | = | = | = | = | = | = | = | = | = | = |

<!-- exact-twin-matrix:cuda:end -->

### SYCL

<!-- exact-twin-matrix:sycl:begin -->

icx release build of `9bc68a108` (no LTO, SYCL AOT `dg2-g11`, xe kernel driver), 2026-10-05: 285 of 288 cells equal, 3 `n/a`.

| backend | feature | 8/420 | 8/422 | 8/444 | 10/420 | 10/422 | 10/444 | 12/420 | 12/422 | 12/444 | 16/420 | 16/422 | 16/444 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| sycl | `adm` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `cambi` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_adm` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_moment` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_motion` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_ms_ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_ms_ssim_chroma` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_ms_ssim_lcs` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_psnr` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_ssim_lcs` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `float_vif` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `motion` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `motion_debug` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `motion_mffw` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `motion_v2` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `motion_v2_mffw` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `psnr` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `psnr_hvs` | = | = | = | = | = | = | = | = | = | n/a | n/a | n/a |
| sycl | `speed_chroma` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `speed_temporal` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `ssimulacra2` | = | = | = | = | = | = | = | = | = | = | = | = |
| sycl | `vif` | = | = | = | = | = | = | = | = | = | = | = | = |

<!-- exact-twin-matrix:sycl:end -->

### HIP

<!-- exact-twin-matrix:hip:begin -->

GCC 16 release build of `9bc68a108` (no LTO, CUDA sm_89 and HIP gfx1036 kernels), 2026-10-05: 285 of 288 cells equal, 3 `n/a`.

| backend | feature | 8/420 | 8/422 | 8/444 | 10/420 | 10/422 | 10/444 | 12/420 | 12/422 | 12/444 | 16/420 | 16/422 | 16/444 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hip | `adm` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `cambi` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_adm` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_moment` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_motion` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_ms_ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_ms_ssim_chroma` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_ms_ssim_lcs` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_psnr` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_ssim_lcs` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `float_vif` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `motion` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `motion_debug` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `motion_mffw` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `motion_v2` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `motion_v2_mffw` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `psnr` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `psnr_hvs` | = | = | = | = | = | = | = | = | = | n/a | n/a | n/a |
| hip | `speed_chroma` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `speed_temporal` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `ssim` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `ssimulacra2` | = | = | = | = | = | = | = | = | = | = | = | = |
| hip | `vif` | = | = | = | = | = | = | = | = | = | = | = | = |

<!-- exact-twin-matrix:hip:end -->
