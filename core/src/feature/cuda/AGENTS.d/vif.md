---
paths:
  - core/src/feature/cuda/integer_vif_cuda.c
  - core/src/feature/cuda/integer_vif_cuda.h
invariant: vif_cuda enforces 16-pixel minimum, reads CPU log2 table, and resets on picture stream.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Integer VIF minimum size, log2 table, and stream reset

- **`vif_cuda` minimum = 16 pixels** via ADR-1324 gate (`context_check`,
  `context_fallback_name = "vif"`), derived from `vif_filter1d_width` in
  `vif_cuda_min_dim()`. `init()` refuses below it BEFORE reading
  `fex->cu_state` (device-free tests pass none).

## `vif_cuda` reads the CPU's log2 table (ADR-1462)

- CPU `vif` reads `log2_table[]` (host libm, `vif_log2_table_generate()`).
  `vif_cuda` reads SAME values: module global `vif_cuda_log2_table`
  (`integer_vif/vif_statistics.cuh`), `log2_lookup(v)` =
  `table[v & (VIF_LOG2_TABLE_SIZE - 1)]`. NO `log2f` / `roundf` in any vif
  kernel source: device `log2f` != host's (307 of 32768 arguments on CUDA
  13.4 vs glibc 2.44; 77 table entries on gfx1036, ADR-1435).
- Host: `init_fex_cuda()` -> `vmaf_cuda_vif_upload_log2_table()` right
  after module load, before buffers; stages table in device buffer, kernel
  `vif_cuda_log2_table_transfer` copies into global, waits on stream. Failure
  -> `vif_init_unwind()`.
- Transfer = kernel, not `cuModuleGetGlobal()`: ffnvcodec loader binds
  legacy symbol, current-API context answers `CUDA_ERROR_INVALID_CONTEXT`.
- Global, not kernel argument: `filter1d.cu` (upstream NVIDIA kernels, four
  baselined HISS-04 function sizes) stays untouched; kernel arithmetic
  unchanged.
- Guards: `test_cuda_vif_log2_table` (device: table empty before upload,
  all 32768 entries == host after, wrong module refused),
  `test_cuda_vif_log2_contract.py` (eight planted regressions),
  `test_cuda_vif_parity`.

- **`integer_vif/filter1d.cu` 16-bit rd-filter upper-bound guard must use
  `(fwidth - fwidth_rd)`, not `(fwidth_rd - fwidth_rd)` (r6-cuda-kernel / 2026-06-04).**
  Correct guard = `fi < (fwidth - (fwidth - fwidth_rd) / 2)`, matching 8-bit
  form at line 183. Writing `(fwidth_rd - fwidth_rd)` (always zero) widens tap
  window to all `fwidth` taps, causes OOB reads into `vif_filt.filter[scale+1]`.
  On rebase: if vertical-pass loop in 16-bit path modified, verify
  upper-bound guard expression before pushing.

- `integer_vif_cuda.c`: reset on the picture stream (scale 0 kernels run there),
  scales 1-3 run on `s->str` after an event recorded behind reset + scale 0,
  DtoH on `s->str`. Reset on `s->str` raced scale 0 under GPU contention: late
  reset erased the first adds, vif scales wrong with >= 2 instances on one
  device (Netflix/vmaf#1305).
