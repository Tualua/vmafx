---
paths:
  - core/src/feature/cuda/integer_cambi_cuda.c
  - core/src/feature/cuda/integer_cambi_cuda.h
invariant: Device-resident CAMBI TVI helper, single readback, and bit-exact CPU contract.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Device-resident CAMBI TVI helper and parameters

- **`integer_cambi_cuda.c` + `integer_cambi/cambi_score.cu` are
  device-resident since ADR-1379** (was Strategy II hybrid, ADR-0360).
  Every stage on device, one 88-byte `CambiCudaResults` readback, one
  wait in `collect_fex_cuda()`. Never re-add host c-values / pooling /
  preprocessing, per-scale readback, or `cuStreamSynchronize` in
  `submit_fex_cuda`. Host constants come from `cambi.c` via
  `cambi_internal.h` (`vmaf_cambi_adjust_window`,
  `vmaf_cambi_mask_index`, `vmaf_cambi_resize_source_indices`,
  `vmaf_cambi_contrast_weights`, `vmaf_cambi_reciprocal_lut`,
  `vmaf_cambi_check_window_fits_lut`, `vmaf_cambi_fixed_topk_mean`),
  shared with SYCL twin. Upstream `cambi.c` refactor renaming them ->
  update `cambi_internal.h`, both twins, same PR.

## Device-resident CAMBI and SpEED (ADR-1379, ADR-1380)

- **One readback, one wait per frame.** `cambi_cuda`: 88-byte
  `CambiCudaResults`; SpEED twins: 40-byte `SpeedGpuFrameResult`. Readback
  on lifecycle private stream behind `lc.submit` event; only wait =
  `vmaf_cuda_kernel_collect_wait()` in collect (`speed_cuda_pipeline_wait()`
  for SpEED). No `cuStreamSynchronize` / `cuCtxSynchronize` / sync copy in
  submit path; no host combine of planes, histograms, c-values, covariance
  or per-block entropies. `core/test/test_cuda_device_resident_contract.py`
  counts readbacks + checks result-block `sizeof`.
- **No per-frame upload.** Twins read engine-uploaded device planes:
  cambi reads `dist->data[0]` on dist stream; SpEED copies planes device to
  device into pipeline raw slots (`speed_cuda_pipeline_stage()`), temporal
  keeps previous frame's luma in other slot (`index % 2`, CPU order).
- **CAMBI c-value** = `__fmul_rn(__int2float_rn(w * p0 * pm), lut[pm + p0])`
  with `vmaf_cambi_reciprocal_lut()` table (42 entries != `1.0f / i`); never
  divide. Top-K = radix select + exact 128-bit sum in 2^-24 units
  (`CAMBI_CUDA_FIXED_SHIFT == VMAF_CAMBI_TOPK_FIXED_SHIFT`, `#error`
  guard); no fp64, no float atomics.
- **Kernel args:** each kernel = one `const <Args> a` struct by value
  (`integer_cambi_cuda.h`, `speed/speed_cuda_params.h`); ADR-1215 (driver
  ignores surplus args). Contract test rejects loose params.
- **Parity contract:** cambi bit-exact whenever CPU's double top-K sum is
  exact (else CPU rounding only; `test_cuda_cambi_parity` compares `==`);
  SpEED bit-exact vs CPU built without FMA contraction and with correctly
  rounded `log2f` (icx build; gcc/glibc differs few frames: glibc 2.43
  <= 4.8e-7, glibc 2.44 <= 1.4e-6; icx `-march=native` up to 7.9e-4,
  Research-1379). Verified on RTX 4090 (Research-1379 finding 8); re-run
  its commands after touching these TUs.
