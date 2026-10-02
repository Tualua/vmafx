- **A clang build with link-time optimisation no longer loses a value its
  caller holds in `xmm0` when the library initialises on a host with
  AVX-512.** `vmaf_init_cpu()` runs one 512-bit instruction on such a host so
  the first frame does not wait for the 512-bit units, as inline assembly
  that declared `zmm0` clobbered. clang drops that declaration in a function
  not compiled for AVX-512; when link-time optimisation (the default build)
  inlined the function into its caller, a floating-point value the caller
  kept in `xmm0` came back as zero. The list now names `xmm0` too, which
  every compiler honours. Seen as a failing unit test (`45 - 20 * log10(x)`
  evaluated to 45) in the hosted `Ubuntu clang` jobs on runners with AVX-512;
  the `vmaf` tool of the same clang 22 build returned the same scores before
  and after. GCC builds were not affected.
