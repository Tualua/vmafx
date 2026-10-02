- **Fourteen core/src and public header files conform to clang-tidy and HISS standards (part 2).**
  The second batch of core library sources and headers (`metadata.h`, `opt.h`,
  `libvmaf/picture.h`, `metadata_handler.h`, `picture_pool.h`, `svm.h`,
  `dict_internal.h`, `fex_ctx_vector.h`, `gpu_picture_pool.h`, `picture.h`,
  `ref.h`, `thread_locale.h`, `x86/cpu.h`, `framesync.h`) were brought
  to zero clang-tidy debt and full HISS compliance under
  [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). Public headers under
  `core/include/libvmaf/` preserve ABI byte-for-byte (HISS-14). Numerical
  correctness is bit-exact across all reference configurations at `--precision max`
  and the Netflix CPU golden gate passes 271/271.
