---
paths:
  - core/test/test_ciede_neon.c
  - core/test/meson.build
invariant: Windows ARM64 MSVC builds tests gating on aarch64 with cl.exe; no POSIX-only headers or calls under ARCH_AARCH64.
---
<!-- markdownlint-disable MD013 -->
# AArch64-gated tests compile under MSVC (ADR-1260)

`Windows ARM64 MSVC` lane builds every test `core/test/meson.build` gates on
`cpu_family()` containing `aarch64` with `cl.exe`, then runs `--suite fast`.
Rules for those files:

- No POSIX-only headers or calls under `#if ARCH_AARCH64`: `<sys/mman.h>`,
  `<unistd.h>`, `sigaction`, `sigsetjmp`, `sysconf`, `_exit`. Windows twin or
  no use.
- Guard-page probes go through `test_ciede_neon.c`'s five entry points
  (`probe_page_size`, `guarded_row_alloc`, `guarded_row_free`,
  `fault_trap_install` / `fault_trap_restore`, `run_kernel_guarded`): POSIX =
  `mmap` + `PROT_NONE` + `sigsetjmp`; Windows = `VirtualAlloc` +
  `PAGE_NOACCESS` + SEH `__try` / `__except`. Copy that shape, do not
  reinvent.
- ADR-1138 `NULL` carve-out applies (MSVC `/std:clatest`, no `nullptr`).
- Local check before push: `meson setup build/aarch64 core --cross-file
  ~/.cache/vmafx-cross/aarch64-clang.ini`, then
  `python3 scripts/ci/run_meson_test.py -- -C build/aarch64 <test>` under qemu.
  MSVC itself: CI only.

- [ADR-0521](../../../docs/adr/0521-msvc-posix-gating-vif-avx512-yuv-input.md) —
  MSVC portability: C source files touched by agents must not use
  bare `__attribute__((noinline, noclone))` without MSVC-guarded
  macro, and must not call `fstat()` / `S_ISREG()` / rely on
  `off_t` being 64-bit without `#ifdef _WIN32` shims established in
  `core/tools/yuv_input.c`. **Rebase-sensitive**: any new `.c` file
  that introduces GCC-extension attributes or POSIX
  `<sys/stat.h>` calls must add matching portability guard or it
  will wedge `Build — Windows MSVC + CUDA` and
  `Build — Windows MSVC + oneAPI SYCL`.
