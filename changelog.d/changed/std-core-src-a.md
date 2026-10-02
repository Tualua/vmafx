- **Fourteen core/src and public header files conform to clang-tidy and HISS standards (part 1).**
  The first batch of core library sources and headers (`pdjson.c`, `dict.cpp`,
  `thread_locale.cpp`, `picture_pool.cpp`, `dict.h`, `picture_pool.c`,
  `model.c`, `model.h`, `framesync.c`, `pdjson.h`, `libvmaf/dnn.h`,
  `libvmaf/model.h`, `libvmaf/libvmaf.h`, `mcp/3rdparty/cJSON/cJSON.h`) were brought
  to zero clang-tidy debt and full HISS compliance under
  [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). Public headers under
  `core/include/libvmaf/` preserve ABI byte-for-byte (HISS-14). Numerical
  correctness is bit-exact across all reference configurations at `--precision max`
  and the Netflix CPU golden gate passes 271/271.
