---
paths:
  - core/test/test_adm_small_border.c
  - core/test/test_adm_wide_rounding.c
  - core/test/test_gpu_adm_tiny_frames.c
  - core/test/test_adm_cm_row_rounding.c
  - core/test/test_adm_cm_row_rounding_contract.py
  - core/test/test_sycl_motion_add_uv_parity.c
invariant: Parity tests register small and 960x540 variants; shared GPU test sources request only names every arm emits.
---
<!-- markdownlint-disable MD013 -->
# Large-fixture parity variants (ADR-1206)

Every CUDA and SYCL parity test is registered twice: once at its
own small fixture and once as `<name>_large` against 960x540, built
from same TU with `-DFIXTURE_W=960u -DFIXTURE_H=540u`. Fixture
macros are `#ifndef`-guarded for exactly this reason — do not
un-guard them.

960x540 is not arbitrary: `min(w, h) = 540` puts shared SSIM/MS-SSIM
auto-scale `max(1, round(min(w, h) / 256))` at 2, and 540 is not
multiple of 16/32-wide kernel blocks, so tail bounds are exercised
too. Below 384 px that auto-scale is always 1 and whole
resolution-dependent half of these extractors is unreachable —
which is where ADR-1202, ADR-1204 and `float_ssim` scale=1-only
limitation all hid.

When adding parity test, add it to matching
`*_parity_large_fixture_tests` list too. Two deliberate exceptions,
both documented in ADR-1206:

- HIP + Metal `test_*_float_ssim_parity` stay registered but treat twin's
  `-EINVAL` at decimating resolution as **skip**, because those twins
  are v1 scale=1-only while CPU decimates — no parity to assert.
  Variant is kept so twin which stops refusing and starts returning
  scale=1 score fails loudly instead of silently comparing two
  metrics. SYCL (ADR-1370) + CUDA (ADR-1399) twins decimate on device:
  their `_large` variants assert parity at auto scale 2, no skip.
- `test_sycl_motion_add_uv_parity` uses scalar fixed-point oracle for
  coefficients, both rounding stages, reflect-101 borders, integer SAD and
  per-plane normalization (ADR-1326). Its error budget is only derived
  host-double `2*gamma_5` reconstruction bound, so both small and large
  registrations are required. Do not restore `float_motion` as numerical
  oracle: it uses different coefficients and float reduction order.

HIP and Metal are not registered yet — unverifiable on current
workstation.

## Shared GPU test sources request only names every arm emits (T-HIP-ADM-TESTS-STALE-SHOULD-FAIL-2026-09-18)

`test_adm_small_border.c` and `test_adm_wide_rounding.c` build twice:
`-DHAVE_CUDA=1` and `-DHAVE_HIP=1`. Feature list must hold only names
arm's twin provides. HIP `integer_adm` twin has no AIM pass, so
`VMAF_integer_feature_adm3_score` / `_aim_score` stay out of its
`provided_features[]` (T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05);
asking for them returns `-EINVAL` from `vmaf_feature_score_at_index()`
before any parity check runs. Guard such names with
`#if !defined(HAVE_HIP)`. Print failing name: bare "failed" cost
session to diagnose. `test_gpu_adm_tiny_frames.c` scores aim / adm3 only in
its SYCL arm (`NUM_KEYS` 7 under `HAVE_SYCL`, bit-exact per ADR-1362);
CUDA arm keeps five keys until someone runs it on CUDA device with seven.

`should_fail : true` in `meson.build` needs reason that is true today.
Meson counts unexpected pass as failure, so stale marker breaks
native test suite on every machine with device. When cited defect is
fixed, drop marker in same PR (ADR-1211 fixed staging fault
three HIP ADM markers cited; markers outlived it by two weeks).

Parity fixtures must carry texture. Smooth ramps such as
`(row * 7 + col * 5) & 0xFF` leave ADM contrast-masking kernel almost
nothing to accumulate: with pre-ADR-1167 border defect planted back
into `adm_cm.hip`, ramp moved adm2 by 7.5e-6, under 1e-4 gate;
lowbias32 texture in `luma_sample()` moves it by 4.0e-4. Before
trusting new parity test, plant defect it targets and watch it
fail. Rounding placement inside row (per pixel, per warp, per row)
does not reach any emitted ADM score: CPU divides accumulator by
`2^(52 - shift_cub - shift_inner_accum)` and casts to `float`. No
score-level tolerance detects it
(T-ADM-CM-ROUNDING-PLACEMENT-UNOBSERVABLE-2026-09-19). Preserve paired
device-free guards: `test_adm_cm_row_rounding.c` exercises private raw
`int64_t` row-fold seam with worked value that separates correct row
rounding (`2`) from per-partition rounding (`4`), truncation (`0`) and
  post-shift increment (`3`); `test_adm_cm_row_rounding_contract.py` binds that
  seam after complete reduction in scalar CPU reference, all 72
  AVX2/AVX-512 band-fold sites, and every CUDA, HIP, SYCL and Metal call shape;
  proves sensitivity with source mutations.
  guards ADR-0155's negative CUDA i4 rounding term against unsigned
  reinterpretation. new backend shape must be added to that contract in
  same change.
