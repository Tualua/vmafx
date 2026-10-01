- **`float_adm` no longer depends on the processor; its scores move by about
  1e-7 on x86.** One step of float ADM divides two wavelet coefficients. On
  x86 the quotient was formed from the processor's reciprocal-estimate
  instruction (`RCPSS`) and one correction step, as upstream Netflix does.
  That instruction is specified by an error bound, not bit for bit, so the
  same frames could score differently on two x86 machines, and differently
  again on ARM and under MSVC, which never used it. `float_adm` now divides
  on every host and with every compiler
  ([ADR-1442](docs/adr/1442-float-adm-reference-divides.md)). Measured on a
  Ryzen 9 9950X3D against the previous build: 147 of 791 `float_adm` scores
  change, by at most 1.3e-7, and the `vmaf_float_v0.6.1`,
  `vmaf_float_v0.6.1neg` and `vmaf_float_4k_v0.6.1` models by at most 1.2e-5
  on a frame and 2.7e-6 on a clip's mean (Netflix 576x324 at 8, 10, 12 and 16
  bits, both 1080p checkerboard pairs, BBB 3840x2160). The fixed-point `adm`
  and the default models do not change, ARM and MSVC builds do not change,
  the Netflix golden tests pass unchanged, and the extractor is not slower.
  `float_adm_cuda` divides as well: it equals the CPU extractor of any
  machine (2034 of 2034 outputs on an RTX 4090) and no longer measures the
  host's instruction when it starts. Scores stored from an x86 build of an
  earlier release differ from new ones by the amounts above.
