- Corrected the usage pages against the code and restructured the longest ones.
  `cli.md` documents every flag of `vmaf --help`, says the progress and pooled
  lines appear only on a terminal, that an unknown `--tiny-codec` exits non-zero
  and that the Sigstore bundle comes from `model/tiny/registry.json`.
  `env-vars.md` gives `VMAF_CUDA_DISPATCH` as `direct|graph`, states that only
  `VMAF_SYCL_USE_GRAPH=1` has an effect, and lists the variables the code reads.
  `ffmpeg.md` covers the full patch series (0001 to 0020), the `-1 = disabled`
  device defaults and the filter options it did not document. `docker.md` lists
  the published images, `bench.md` the three features `--validate` checks, and
  the worked examples show the current scores (76.667831 with `vmaf_v0.6.1`,
  82.816060 with the default model).
