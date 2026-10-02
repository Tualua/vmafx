- **`float_adm` and the models that read it use Netflix's CSF weights again;
  `vmaf_float_v0.6.1` moves by up to 2.2e-5 per frame.** Two inherited
  routines compute the contrast-sensitivity weights of float ADM: the Watson
  quantisation step (`adm_tools.h`) and the Barten model (`barten_csf_tools.h`,
  `adm_csf_mode=1`). Netflix keeps their intermediates in `float`; fork ports
  (#552, #760, #44) had widened them to `double` to quiet a static-analysis
  finding, which moved every weight by a few units in the last place. The
  fork goes back to Netflix's arithmetic
  ([ADR-1489](docs/adr/1489-float-adm-barten-upstream-float.md)). The one
  remaining difference from Netflix's `float_adm` on x86 is the division this
  fork chose in ADR-1442: against a Netflix build with the plain quotient,
  every `float_adm` value under 36 option sets and the score of every float
  model are identical on 658 measured frames, at scalar, AVX2 and AVX-512
  dispatch. What you see: `float_adm` scores move in the seventh decimal
  (`adm2` by at most 1.1e-7, the per-scale scores by at most 2.7e-7), the
  float models (`vmaf_float_v0.6.1`, `vmaf_float_v0.6.1neg`,
  `vmaf_float_4k_v0.6.1`, `vmaf_v0.6.0`) in the fifth (at most 2.7e-5 on a
  frame, 1.5e-5 on a clip's mean), and fixed-point `adm` with
  `adm_csf_mode=1` in the seventh (at most 1.6e-7); on the CPU and on the
  CUDA, SYCL, HIP and Metal twins alike. The default models and fixed-point
  `adm` in its default mode do not change. The Netflix golden gate passes
  unchanged.
