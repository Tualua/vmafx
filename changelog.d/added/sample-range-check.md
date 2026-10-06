- **Opt-in sample range check: `vmaf --check-sample-range` and
  `vmaf_set_sample_range_check_enabled()`.** A 10- or 12-bit picture stores
  its samples in 16 bits, so it can carry values above 2^bpc - 1, which are
  invalid input (the CPU extractors and their GPU twins may then score
  differently). With the check on, `vmaf_read_pictures()` refuses such a frame
  with `-EINVAL` before extracting anything and logs the picture, plane, row,
  column and value; the command line stops with a non-zero exit status. Off by
  default, at the cost of one flag test per frame
  ([ADR-1918](docs/adr/1918-sample-range-contract-opt-in-check.md),
  [Sample range](docs/api/sample-range.md)).
