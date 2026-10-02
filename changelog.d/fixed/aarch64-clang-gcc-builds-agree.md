- **An aarch64 clang build and an aarch64 GCC build return the same scores
  ([ADR-1461](docs/adr/1461-strict-fp-every-translation-unit.md)).** clang
  fuses `a * b + c` into one multiply-add by default and GCC does so for C++;
  on aarch64, where the instruction is baseline, the feature library, the SVM
  and the model code were built that way, each compiler in its own places.
  The two builds differed on 680 of 3355 measured values: `speed_chroma` by up
  to 6.2e-5, `float_vif` scales by up to 3.5e-5, `speed_temporal` by 2.3e-2 on
  a checkerboard. Every C and C++ file is now built without contraction (a
  project-wide compiler argument), and the builds agree on 3350 of the 3355;
  the five left are `ciede2000` values 1.1e-12 apart that differ between the
  compilers on x86-64 too. x86-64 builds are unchanged. Scores from an
  existing aarch64 clang build (macOS, Linux on ARM) differ from a new one by
  the amounts above. `make test-netflix-golden-arm64` runs the Netflix golden
  gate against an aarch64 GCC or clang cross build under qemu-user on an x86
  host.
