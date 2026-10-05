- **`integer_adm_metal` returns the CPU's `adm` scores again
  ([ADR-1806](docs/adr/1806-metal-kernels-host-replay.md)).** The first
  report of the macOS tester bundle on an Apple M4 Pro (issue #2118) showed
  every `adm` output off on every frame, by up to 2.8. Six defects caused it:
  the reduction kernels wrote their sums twice as far apart as the host read
  them (losing one band and writing past the buffer), scale 1 read the 16-bit
  band of scale 0 as 32-bit samples, the scales-1-3 masking terms and
  denominator rounded differently from the CPU, `adm_skip_scale0` left an AIM
  numerator at scale 0, and the noise floor multiplied in single precision.
  The twin now shares one definition of its uniforms and reduction layout
  between kernels and host, reads the 16-bit band at scale 1, and takes every
  shift, rounding term and score formula from the CPU extractor's own code. A
  new host test, `test_metal_integer_adm_host_replay`, runs the unmodified
  Metal kernels on Linux and macOS through a shim and compares every output
  with the CPU at `==`; each of the six defects makes it fail. The fix is not
  yet measured on an Apple GPU.
