- **Integer ADM and the models that read it return Netflix's values again;
  `vmaf` moves by up to 2e-5.** The Watson quantisation step of integer ADM
  raises 10 to `k * temp * temp`. Netflix multiplies the three `float`s in
  `float`; a static-analysis sweep (#552) had widened the product to
  `double`, which moved the CSF weights by one to three units in the last
  place and with them every integer ADM score. The fork goes back to
  Netflix's expression
  ([ADR-1475](docs/adr/1475-integer-adm-quant-step-upstream-float.md)).
  Measured against Netflix master `cea2b4d8` on 31 clips at `%.17g`:
  `vmaf_v0.6.1` is identical on 504 of 504 frames (32 before, up to 1.83e-5
  apart), as are `vmaf_v0.6.1neg`, `vmaf_4k_v0.6.1` and the bootstrap model
  `vmaf_b_v0.6.3`; `integer_adm2` is identical on every decoded picture. What
  you see: `integer_adm2`, `integer_adm3`, the per-scale ADM scores and the
  `vmaf` of those models move in the fifth or sixth decimal on some frames
  (59 of 720 values of the 576x324 snapshot, at most 2e-5), on the CPU and
  on the CUDA, SYCL, HIP and Metal twins alike. The Netflix golden gate
  passes unchanged. The `vmaf_v1.0.16` and float models are not affected by
  this change.
