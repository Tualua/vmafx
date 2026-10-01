- **The dev and SYCL + FFmpeg containers build the CPU code with GCC.**
  `dev/Containerfile` and `Containerfile.vmafx` configure libvmaf with
  `CC=gcc CXX=icpx` and `-Db_lto=false`; the SYCL kernels and the other C++
  code stay with icpx. When the C++ compiler is icpx and the C compiler is
  not, every C++ translation unit now gets icpx's strict floating-point
  options (`-fp-model=precise -ffp-contract=off`), as in an all-icx build, and
  with SYCL enabled the test executables link as C++. CPU scores are those of
  a GCC build
  ([ADR-1593](docs/adr/1593-hybrid-gcc-cpu-icpx-sycl.md)).
