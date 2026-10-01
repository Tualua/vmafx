<!-- markdownlint-disable MD013 MD060 -->
# ADR-1431: `vmaf_read_pictures()` owns the pictures it is given on every return

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: api, ownership, cuda, picture-pool, cli, fork-local

## Context

Netflix/vmaf#1420 reports that `vmaf_cuda_buffer_alloc()` asserts when the device is out of memory. The fork does not abort: `CHECK_CUDA` returns the error since the wholesale rewrite, and `test_cuda_buffer_alloc_oom` pins `-ENOMEM`. Reproducing the report on the fork (RTX 4090, a second process holding all but 400 MiB, `vmaf --backend cuda` on a 4096x2160 pair) found a different failure: the CLI prints

```text
CUDA error at ../core/src/cuda/picture_cuda.c:359: CUDA_ERROR_OUT_OF_MEMORY (2) in cuMemAllocPitch(...)
libvmaf ERROR problem during prepare_ring_buffer
problem reading pictures
```

and then never exits. It sat at 0.1 % CPU while holding the device lock until it was killed. Attached to the process, the main thread waits in `vmaf_close()` -> `vmaf_commit_remaining_owners()` -> `vmaf_picture_pool_close()` -> `pthread_cond_wait()`: the pool closes only when every picture has come back, and the pair of the failed `vmaf_read_pictures()` never did.

The cause is in `vmaf_read_pictures()`. The pair came from the context's picture pool (the CLI preallocates one). The call returned on a failure of its validation step (a non-increasing index, pictures whose shape or bit depth disagree with the stream, the picture pool, the CUDA ring buffer, the SYCL host-upload preparation) and on a failure of the CUDA translation without releasing the pictures, while every failure after that point (an extractor, the post-extractor stage, a failed context fallback) releases them. The documented rule said the opposite of both: `docs/api/index.md` stated that on an error the caller keeps the pictures and must unref them, `libvmaf.h` said the context takes ownership without exception, and `test_read_pictures_monotonic` and `test_validate_pic_params_bpc` unref after a rejection. No caller follows the first rule: the `vmaf` CLI, `vmaf_bench`, the MCP `compute_vmaf`, `vmaf_vpl` and the `libvmaf_tune` filter in `ffmpeg-patches/` leave the pictures alone after an error. A caller that did follow it would unref pictures an extractor failure had already released.

## Decision

We will make the context own both pictures from the moment `vmaf_read_pictures()` is called with a context and two pictures, whatever the call returns: every return releases them exactly once, the CUDA translations included, and the call keeps the order of its checks. A call without a context, or with only one of the two pictures, still returns `-EINVAL` and takes nothing, as does the flush call (both pictures `NULL`). `docs/api/index.md` and the header state the rule; the two tests that unref after a rejection stop doing so.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Caller keeps the pictures on every error (the rule `docs/api` stated) | Matches the old prose; a caller could retry the same pair | Every in-tree and FFmpeg caller would have to unref after a failure; the extractor and post-extractor failure paths would have to stop releasing, and a pair an extractor already holds (a GPU extractor takes its own reference in `submit()`) could not be reclaimed anyway | A larger change that touches every caller and every failure path, for a retry nobody does |
| Caller keeps the pictures after a validation rejection, the context owns them after any other failure | Keeps what `test_read_pictures_monotonic` pinned | A caller cannot tell the two apart: `-EINVAL` is returned by a validation rejection and by an extractor failure on a non-finite score | A rule the caller cannot apply |
| Fix only the CLI (release the pair itself when a read fails) | No libvmaf change | The CLI cannot know whether the library released it; the extractor failure paths would then release twice, which corrupts the pool free list | Wrong for half the failures |

## Consequences

- **Positive**: one rule a caller can follow, the one every caller already follows; an out-of-memory or a rejected call leaves the picture pool whole, so `vmaf_close()` returns and the `vmaf` CLI exits with a non-zero status instead of hanging with the device lock.
- **Negative**: a program written to the old prose (unref after an error) now unrefs a released picture. Nothing in the tree did, and the prose was already unsafe against the extractor failure paths.
- **Neutral / follow-ups**: `test_read_pictures_failure_ownership` (CPU) pins the rule for a rejected index, mismatched pictures and a flushed context with a pool that has no spare picture; `test_cuda_oom_pictures_released` pins the out-of-memory case on a device.

## References

- Netflix/vmaf#1420; the fork's reproducer is `/home/kilian/.cache/vmafx-upstream-rebase/evidence/c1420/hog.c` with the CLI line above.
- Per user direction (2026-10-01): check on the fork whether the upstream defects found on `6ec23e8f2` reproduce, and fix those that do (RC3, [ADR-1421](1421-rc3-rc8-candidate-map.md)).
