---
paths:
  - core/src/libvmaf.c
  - core/src/feature/feature_extractor.cpp
invariant: PREV_REF batch dispatch unrefs before memset; vmaf_read_pictures owns both pictures on return.
---
<!-- markdownlint-disable MD013 -->
# Picture ownership, batch dispatch, and SYCL upload synchronization

## PREV_REF batch dispatch: unref before memset, zero f->prev_ref (ADR-1072)

`threaded_extract_batch_func` in `libvmaf.c` feeds PREV_REF extractors by
copying `f->prev_ref` into `fex->prev_ref` via bare struct copy (no
`vmaf_picture_ref` — VmafRef* is shared, not reference-counted
separately).  After `vmaf_feature_extractor_context_extract()`:

- **SUCCESS**: PREV_REF SWAP in `feature_extractor.cpp` has decremented
  old-frame VmafRef (via struct-copy alias) and bumped current
  frame into `fex->prev_ref` with extra refcount.
- **ERROR**: `fex->prev_ref` is unchanged (still struct-copy alias).

**In both cases**: call `vmaf_picture_unref(&fex->prev_ref)` before
`memset(&fex->prev_ref, 0, ...)` to release that counted reference.  Then
call `memset(&f->prev_ref, 0, ...)` to prevent `unref:` block at
bottom of function from double-freeing now-consumed VmafRef.

Bare `memset` without prior unref leaks one picture-pool slot per
PREV_REF frame, exhausting pool and deadlocking
`vmaf_picture_pool_fetch` in `pthread_cond_wait` after ~pool_size frames.

Serial path (`read_pictures_dispatch_one`) uses `vmaf_picture_ref` for
copy (ADR-0778) and already calls `vmaf_picture_unref` before memset;
these two paths must stay consistent.

**Rebase-sensitive**: any branch that re-opens or modifies
`VMAF_FEATURE_EXTRACTOR_PREV_REF` block in `threaded_extract_batch_func`
must preserve both unref-before-memset and zero-f->prev_ref.

## `vmaf_read_pictures()` owns both pictures on every return (ADR-1431)

- Context + two pictures given -> every return releases both once. No early
  `return err;` between argument checks and extractor loop: pool slot leaks,
  `vmaf_picture_pool_close()` waits forever in `vmaf_close()`, `vmaf` CLI hangs
  holding the device lock after a VRAM out-of-memory (Netflix/vmaf#1420 on fork).
- Validation + prep failures -> `read_pictures_frame_cleanup()`. CUDA translation
  failure -> `read_pictures_translate_abort()`: translations sharing `priv` with
  caller's picture released with it, fresh ones (ring pictures, downloaded host
  copy) released here, fresh device stream drained first (upload reads the host
  picture).
- No context / one picture `NULL` / flush call (both `NULL`) -> takes nothing.
- Guards: `test_read_pictures_failure_ownership` (CPU, pool with no spare
  picture, alarm turns hang into failure), `test_cuda_oom_pictures_released`.

## SYCL shared uploads finish before either picture cleanup returns (BUG-040)

`vmaf_sycl_shared_frame_upload()` reads caller's host-backed reference and
distorted pictures asynchronously on in-order `copy_queue`. Its saved
`last_upload_event` is final distorted-plane copy, so waiting on that one
event also orders every earlier reference and distorted copy without draining
independent compute queue.

Both ownership exits in `libvmaf.c` must preserve that wait:

- `read_pictures_frame_cleanup()` waits before its direct picture unrefs.
- `threaded_read_pictures_batch()` waits after enqueue while caller's
  original counted references are still live, then unrefs those references.
  worker may finish and drop its own copies before wait, so moving
  barrier to `read_pictures_frame_cleanup_after_batch()` is too late:
  release callback can already have poisoned or recycled host storage.

Do not replace either event wait with global queue/device wait, and do not
remove threaded wait because one timing sample happened to let DMA finish
before worker. In combined CUDA+SYCL build, wait must remain before
CUDA's host-cleanup early return. `core/test/test_sycl_cuda_serial_upload_lifetime.c`
pins that compile combination through public API with `n_threads=0` and
`n_threads=1`; 4K release callback poisons host storage as soon as its final
reference drops and PSNR proves DMA already consumed original pixels.
`testdata/test_sycl_4k_repeat_determinism.py` then covers 20 serial and 20
`--threads 1` runs against full normalized score report.
