---
paths:
  - core/src/dnn/tensor_io.c
  - core/src/dnn/tensor_io.h
  - core/src/dnn/ort_backend_internal.h
invariant: Tensor helpers mirror ONNX element types and f16 subnormal conversion avoids unsigned arithmetic overflow.
---
<!-- markdownlint-disable MD013 -->
# Tensor I/O and Datatype Conversions

- **ImageNet normalisation lives in graph**, not in C helper —
  exporters absorb inverse transform so C side feeds tensors from
  shared `vmaf_tensor_from_rgb_imagenet()` helper unchanged. See
  [ADR-0041](../../../../docs/adr/0041-lpips-sq-extractor.md).

## Rebase-sensitive invariants (DNN-side surfaces in flight)

- **`ort_backend_internal.h` elem-type accessors mirror ONNX enum values**
  (`VmafOrtElemType UNDEFINED=0 / FLOAT=1 / FLOAT16=10`): numeric values
  deliberately identical to `ONNXTensorElementDataType` so cast
  comparisons in `ort_backend.c` are safe. Both `VMAF_HAVE_DNN` path
  (reads `sess->input/output_elem_types[slot]`) and `!VMAF_HAVE_DNN`
  stub (returns `ELEM_TYPE_UNDEFINED`) must provide `vmaf_ort_internal_input_elem_type`
  and `vmaf_ort_internal_output_elem_type`; removing either breaks
  `test_ort_internals.c` link on no-ORT builds, blocks Netflix CPU
  Golden Tests (D24) CI job at build step. See PR
  `fix/dnn-ort-internals-missing-elem-type-accessors` (2026-06-03).
- **`f16_to_f32_one` subnormal path uses `int32_t exp_adj`, not
  `uint32_t exp`** (fork-local, round-5 `-fsanitize=integer` sweep,
  PR fix/picture-align-unsigned-narrowing): normalisation loop in
  `tensor_io.c:f16_to_f32_one` iterates local `int32_t exp_adj = 1`
  counter bounded to `[-9, 1]` (10-bit f16 mantissa). Earlier
  implementation used `uint32_t exp` variable from outer scope,
  wrapped through `UINT32_MAX` twice to produce
  correct f32 biased exponent by modular arithmetic — functionally
  correct but trips `-fsanitize=integer`. Never revert to `uint32_t`
  wrap idiom. `test_f16_to_f32_subnormal` test asserts exact
  bit-pattern for `0x0001` (smallest positive f16 subnormal, value
  `2^-24`) to catch any accidental regression. See
  [docs/rebase-notes.md](../../../../../docs/rebase-notes.md)
  §PR-fix-picture-align-unsigned-narrowing.
