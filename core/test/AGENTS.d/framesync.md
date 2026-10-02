---
paths:
  - core/test/test_framesync_init_failure.c
  - core/test/test_framesync.c
  - core/test/meson.build
invariant: Framesync initializer failures recompile framesync.c into test executable; target keeps LTO off on Darwin.
---
<!-- markdownlint-disable MD013 -->
# Framesync initializer tests and LTO configuration

- **Framesync initializer failures recompile `framesync.c` into test
  executable.** `test_framesync_init_failure` lists `../src/framesync.c` among
  its own sources and maps four init/destroy entry points to test-owned
  wrapper symbols through `c_args`. Those `-D` renames reach every translation
  unit in target, so `test_framesync_init_failure.c` undefines them above
  its first `#include`. Keep test source ordinary translation unit: do
  not include executable `.c` files, add production hook, or replace
  deterministic wrappers with platform-specific linker interposer. test
  must cover each initialization stage, null init-output pointer,
  documented null destroy no-op, unpublished context, and exact partial
  unwind.
- **`test_framesync_init_failure` builds with LTO off on Darwin, and that is
  load-bearing.** **Rebase-sensitive**: keep
  `override_options : framesync_interposer_lto_override` on target. Under
  `b_lto=true` (project default) this executable's whole input is LLVM
  bitcode, and Apple's linker rejects result on all five macOS legs with
  `ld: symbol(s) defined in LTO objects are referenced but missing in compiled
  objects`, naming `_mu_tests_run`, the four `_vmafx_test_pthread_*` wrappers,
  `_vmaf_framesync_init` and `_vmaf_framesync_destroy`. Two narrower fixes were
  tried against that diagnostic and both failed byte-identically: removing
  `link_whole`/`-force_load` (34b3ffa09) and removing `vmaf_cflags_common` so
  definitions keep default visibility (18e9edebd).
  Replaying target compile and link commands through clang/LLD shows LTO resolution
  hands libLTO one externally visible symbol (entry point); generated object
  comes back with `main` as only global. Pure-bitcode sibling targets link earlier
  in same macOS run (`test_pdjson`, `test_thread_pool`,
  `test_pdjson_stack_increment_zero`); they collapse identically and link fine.
  *why* Apple's linker singles this one out is not established. Do not spend
  fourth attempt steering preserve list without macOS machine to test on;
  `b_lto=false` removes precondition instead, same way `test_output`
  below already handles its own Apple-LTO problem. Also do not reintroduce
  `framesync.c` as `static_library` consumed with `link_whole`.
