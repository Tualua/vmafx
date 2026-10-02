- **The predicted `vmaf` score of an icx-built binary moves by up to 7e-12 and
  now matches a GCC build except where Intel's math library differs.** Since
  the strict floating-point flags became a project-wide compiler argument
  ([ADR-1461](docs/adr/1461-strict-fp-every-translation-unit.md), #1829), an
  icx build compiles `svm.cpp`, `predict.c`, `model.c` and `libvmaf.c` with
  `-fp-model=precise -ffp-contract=off`; before, they took `-O3` alone, which
  under icx is its fast floating-point model. No extractor value changed. The
  model score changed on every frame with a non-zero score (160 of 163
  measured frames), by at most 7.05e-12, towards the GCC build: frames whose
  features are identical in both builds but whose `vmaf` differs went from 153
  of 163 to 15 of 163 (Netflix 576x324 frame 42: 83.13509129537665 before,
  83.1350912953696 after, the GCC value). GCC builds did not move. What
  still separates an icx build from a GCC build is Intel's math library
  (`libimf`). The published container images are built with icx; scores
  printed at the default `%.6f` are not affected by a change of this size.
  See
  [build flags](docs/development/build-flags.md#floating-point-contraction-is-off-everywhere).
