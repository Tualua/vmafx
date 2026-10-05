- **The ADR allocator and its tests meet the HISS shell rules (ADR-1142).**
  `scripts/adr/next-free.sh` and its three tests handle the exit status of every
  command they used to discard with `|| true`: a grep that finds nothing is
  accepted by status, a failed fetch or ADR-claim cleanup prints a warning,
  local files are listed by testing that they exist, and the shallow-safety test
  sources the extracted function instead of `eval`ing it. The allocator picks
  the same numbers as before. The HISS baseline loses 31 infractions.
