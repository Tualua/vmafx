- Rust CI now runs `cargo fmt --all --check` and `cargo clippy --workspace --all-targets -- -D warnings`.
  Until now only `vmafx-sys` was linted; `vmafx`, `vmafx-tad` and any crate added to the
  workspace are covered without a workflow edit (`docs/development/rust.md`, "Linting").
