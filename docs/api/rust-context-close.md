# Rust context close and retry

The safe Rust wrappers treat context teardown as an ownership transition, not
as an infallible destructor. This matters for GPU-backed contexts: libvmaf can
return an error while it still owns streams, modules, and model references.

Both Rust crates expose the same close protocol:

| Crate | Active context | Retry-only error token |
| --- | --- | --- |
| `vmafx` | `Context<'a>` | `ContextCloseError<'a>` |
| `vmafx-sys::safe` | `VmafContext<'a>` | `VmafContextCloseError<'a>` |

Call `close(self)` when teardown errors need to be handled explicitly. Exact
zero is the only success value. Any nonzero result, including an unexpected
positive status, returns the retry-only token and preserves the native context:

```rust
match context.close() {
    Ok(()) => {
        // The native context and all registered-model borrows are released.
    }
    Err(pending) => {
        eprintln!("initial close failed: {}", pending.error());
        match pending.retry() {
            Ok(()) => {
                // The retry completed teardown.
            }
            Err(still_pending) => {
                eprintln!("close retry failed; first error: {}", still_pending.error());
                // The one-retry budget is exhausted. Dropping this token
                // aborts without making a third native close call.
                drop(still_pending);
            }
        }
    }
}
```

After `close` starts, scoring cannot resume. The error token deliberately
exposes only `error()` and the consuming `retry()` method. A failed retry
returns the token again, but the one-retry budget is then exhausted. `error()`
continues to report the first close error; the retry result never replaces it.
Calling `retry()` again does not call native code and returns the same token.

libvmaf owns a reference to every model registered with a context (ADR-1755),
so a model no longer has to outlive a teardown-pending native context. The `'a`
lifetime on `Context` and on the retry token is kept so existing code compiles
unchanged; it is now conservative, not load-bearing for soundness.

## Drop behavior

Explicit `close` is recommended whenever the application can report or recover
from teardown failures. `Drop` never aborts the process:

- Dropping an active context makes a close attempt and one bounded retry.
- Dropping a fresh retry token consumes its one permitted retry.
- If the retry fails, or the token was already retried, the native context is
  leaked: no further close call is made, one line goes to stderr, and the
  process carries on. The leaked context holds no pointer into memory Rust can
  free, because libvmaf owns a reference to each model it mounted.

The low-level raw FFI (`use vmafx_sys::*`) does not provide this ownership
guard. Raw callers must keep the `VmafContext` pointer and every imported
backend dependency alive until `vmaf_close` returns exactly zero. Models are
owned by the context once registered.

See the [Rust development guide](../development/rust.md) for crate setup and
the [`vmaf_close` C contract](lifecycle.md#close-and-retry) for the underlying
native API.
