- **Two workflows, one Rust example and the NIQE moment fit meet the HISS
  rules.** `docker-image.yml` and `pr-type-label.yml` no longer discard a
  command's exit status with `|| true` (a failed label call now prints a
  warning), the `vmafx-sys` `score` example returns an error instead of
  calling `process::exit`, and `niqe_extract_aggd()` is split into three
  helpers with the same float operations in the same order (NIQE scores are
  byte-identical on the Netflix pair). The HISS baseline loses 6 infractions.
