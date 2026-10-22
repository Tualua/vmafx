<!-- markdownlint-disable MD013 -->

# ADR-1755: The feature collector owns the models it mounts

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: Lusoris
- **Tags**: api, abi, model, ownership, rust, hiss, fork-local

## Context

`vmaf_use_features_from_model()` mounts the caller's `VmafModel *` on the
feature collector (`libvmaf.c`, `vmaf_feature_collector_mount_model()`), which
keeps the bare pointer in a `VmafPredictModel` node. The collector reads it
again whenever a metadata handler is registered: `vmaf_feature_collector_append()`
calls `feature_collector_run_model_predict()`, which reads `model->name` and
runs `vmaf_predict_score_at_index()`. So the public contract was "borrowed
until `vmaf_close()` returns exactly 0", and a `vmaf_close()` that fails part-way
leaves the collector, and its pointer, alive.

The Rust `Drop` of a context whose close failed twice could not honour that
contract (returning from `Drop` ends the model borrow), so it called
`std::process::abort()`: four HISS-07 rows in `bindings/rust`. An audit of every
stage of `vmaf_close()` (`vmaf_prepare_close`, `vmaf_commit_extractor_owners`,
`vmaf_commit_remaining_owners`) found that the mounted model pointer is the only
pointer into caller-owned memory a failed close can leave behind: option
dictionaries are deep-copied (`dict.cpp` `dict_append_new_entry`), the
configuration is scalar-only, pictures are libvmaf-owned and counted per worker
job, and the Rust crates register no callbacks. A test frees the caller's model
and then appends a score with a metadata handler registered; under ASan it
reports a heap-use-after-free in `feature_collector_run_model_predict()` on the
unfixed tree, with and without a failed close.

## Decision

The collector owns what it mounts. `VmafModel` carries an owner count
(`struct VmafRef *owners`, created by the JSON loaders with one owner, the
caller). `vmaf_feature_collector_mount_model()` takes one owner
(`vmaf_model_ref()`), `unmount` and `vmaf_feature_collector_destroy()` drop it,
and `vmaf_model_destroy()` drops one owner and frees the model with the last. A
caller may therefore destroy its model right after
`vmaf_use_features_from_model()` succeeds. The public API changes additively
(HISS-14): code that keeps the model alive until `vmaf_close()` returns 0 is
unaffected. Models that nothing loaded (`owners == NULL`) keep the single-owner
behaviour and cannot be mounted (`-EINVAL`).

The Rust `Drop` of a context after a persistent close failure then logs one
line and leaks the context instead of aborting, in both crates; the `'a` model
lifetime stays for source compatibility.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Reference count on `VmafModel` (chosen) | Constant-time, no copy of the SVM model; reuses `VmafRef`; models stay shared and read-only | A field in the internal struct; both loaders must create the count | Smallest change that removes the borrow |
| Deep copy of the model at mount | Collector fully independent | `svm_model`, option dictionaries and knots would be duplicated per context; a second copy path to keep in step with every model field | Cost and a second implementation of "copy a model" (HISS-19) |
| Keep borrowing, keep `abort()` | No libvmaf change | Rust `Drop` terminates the process; HISS-07 rows stay | Rejected by the maintainer ("fix the root before rc.3") |
| Keep borrowing, leak the context in Rust | No libvmaf change | Unsound for a raw-FFI caller and for any metadata handler: the leaked collector still dereferences a model Rust may free | The audit finds the retained pointer |

## Consequences

- **Positive**: no use-after-free from a model destroyed early or after a failed
  close; the four `process::abort` rows disappear; `Context<'a>` no longer needs
  its borrow for soundness.
- **Negative**: one more atomic counter per model; the model outlives the
  caller's `vmaf_model_destroy()` by as long as a context holds it.
- **Neutral / follow-ups**: `vmaf_model_collection_*` ownership is unchanged
  (the collection owns its members; mounting a collection mounts each member,
  taking an owner each). FFmpeg filters that destroy their model after close are
  unaffected. Documentation: `libvmaf.h`, `docs/api/lifecycle.md`,
  `docs/api/models-and-features.md`, `docs/api/rust-context-close.md`.

## References

- Maintainer decision 2026-10-05 (popup "Fix the root before rc.3"): paraphrased,
  make libvmaf own the models it mounts and remove the abort from the Rust crates.
- [ADR-1336](1336-cuda-context-owned-resource-teardown.md): the retryable `vmaf_close()` ownership contract this builds on (unchanged).
- `core/test/test_collector_owns_mounted_model.c`: failing first, then green
  under ASan and UBSan.
