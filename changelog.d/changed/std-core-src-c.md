- **Fourteen core/src and public header files conform to clang-tidy and HISS standards (part 3).**
  The third and final batch of core library sources and public headers
  (`libvmaf/feature.h`, `cpu.h`, `mem.h`, `output.h`, `percentile.h`,
  `predict.h`, `read_json_model.h`, `thread_pool.h`, `libvmaf/libvmaf_cuda.h`,
  `x86/cpu.c`, `gpu_picture_pool.cpp`, `arm/cpu.h`, `log.c`, `log.h`) were
  brought to zero clang-tidy debt and full HISS compliance under
  [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). In `x86/cpu.c`, function
  nesting depth was refactored with guard returns to satisfy
  `readability-function-size`. Public headers under `core/include/libvmaf/`
  preserve ABI byte-for-byte (HISS-14). Numerical correctness is bit-exact across
  all reference configurations at `--precision max` and the Netflix CPU golden
  gate passes 271/271.
