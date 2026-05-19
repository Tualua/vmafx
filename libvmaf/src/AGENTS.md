# AGENTS.md — libvmaf/src

Scoped orientation for any coding agent working directly inside `libvmaf/src/`.
Parent scope: [`../AGENTS.md`](../AGENTS.md) (libvmaf) and
[`../../AGENTS.md`](../../AGENTS.md) (root).

## Mandatory safety invariants

The following invariants were established during the 2026-05-16 memory-safety
audit (findings #7, #8, #10). Every PR that touches the affected files — or
adds new code in the same category — must preserve them.

### 1. Every `pthread_*_init` return value must be checked (finding #7)

`pthread_mutex_init`, `pthread_cond_init`, and `pthread_rwlock_init` return
non-zero on `ENOMEM` on some POSIX implementations (embedded, musl-based
systems). Ignoring the return value leaves the pool or lock object in an
undefined state; the next `pthread_mutex_lock` call is undefined behaviour.

Pattern to follow: staged init with teardown of already-initialised
primitives on failure (see `vmaf_thread_pool_create` in
[`thread_pool.c`](thread_pool.c)).

### 2. Every `aligned_malloc` / `malloc` call must NULL-check before use (finding #8)

A missing NULL check after `aligned_malloc` causes a null-pointer dereference
on OOM (ASan-detected). In hot-path functions such as `adm_dwt2_*` in
[`feature/adm_tools.c`](feature/adm_tools.c), the allocation must either be
NULL-checked and the function must return an error, or the buffer must be
pre-allocated in the extractor `init` callback so the per-frame path stays
allocation-free. See Power of 10 rule 3 and CERT MEM30-C.

### 3. Size-computing functions that accept external `unsigned w` / `h` must bound-check first (finding #10)

When `w` is large enough that `(w + ALIGN - 1u)` wraps on `unsigned`
arithmetic, the resulting aligned size is 0. The allocator succeeds, and any
pixel read is OOB. Add an early-exit `if (w == 0 || w > 32768u || ...)
return -EINVAL;` guard at the public entry point before any arithmetic.
Pattern: see `vmaf_picture_alloc` in [`picture.c`](picture.c). CERT INT30-C.

### 4. Capacity bounds checks in `output.c` must use `>=`, not `>` (ADR-0606)

All frame-iteration loops in [`output.c`](output.c) guard per-feature
access with:

```c
if (i >= fc->feature_vector[j]->capacity)  /* ADR-0606: >= not > */
    continue;
```

The allocated score array covers indices `0..capacity-1`. Index `capacity`
is one past the end. Using `>` (strictly greater) allows access at
`i == capacity`, which is a heap buffer overread (UB). Under
`MALLOC_PERTURB_=198` (the macOS CI setting), the poisoned byte at
`score[capacity].written` is `0xC6` (truthy), causing spurious "written"
results and downstream SIGSEGV under Apple Clang's UB optimizations.

If an upstream sync or cherry-pick replaces any of the 7 capacity-check
sites with `>`, revert back to `>=` in the same commit.

### 5. Comma-tracking in JSON writers must use explicit `bool first` flags (ADR-0606)

`json_write_pool_score` and `json_write_frames` in [`output.c`](output.c)
track whether a comma separator is needed via explicit `bool first` /
`bool first_frame` flags. Do not replace these with:

- `j > 1` (pool method enum) — wrong when `j == 1` call is skipped and
  `j == 2` is first, producing a leading comma in the JSON object.
- `i > 0` (frame index) — wrong when frame 0 has no written scores and
  frame 3 is first, producing a leading comma in the JSON array.
