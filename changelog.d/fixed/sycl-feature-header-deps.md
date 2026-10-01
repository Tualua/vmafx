- **Editing a SYCL header now rebuilds the kernels that include it.** The
  build compiled each SYCL source through a custom target that tracked only
  the source file. After a change to a header such as
  `core/src/feature/sycl/sycl_exact_fp.h`, `ninja` reported nothing to do and
  the library kept the kernels built from the old text; only a clean build
  picked the change up. The targets now record the headers each source
  includes (compiler depfiles, as the CUDA and HIP kernels have had since
  ADR-1320). Builds from a clean tree, such as CI and the release container,
  were not affected. Not yet on Windows, which builds from clean.
