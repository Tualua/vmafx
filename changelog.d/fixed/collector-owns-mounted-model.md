- **A model registered with `vmaf_use_features_from_model()` can be destroyed
  at once.** The context's feature collector now owns a reference to every
  model it mounts (ADR-1755), so a model destroyed after registration, or
  before a `vmaf_close()` that fails and is retried, is no longer read after it
  is freed (a heap-use-after-free found with a metadata handler registered). The
  change is additive: code that keeps the model alive until `vmaf_close()`
  returns 0 behaves as before. The Rust crates' `Drop` of a context whose close
  failed twice now leaks the context and prints one line to stderr instead of
  calling `process::abort()`. Documented in `docs/api/lifecycle.md`,
  `docs/api/models-and-features.md` and `docs/api/rust-context-close.md`.
