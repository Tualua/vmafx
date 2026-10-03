- `docs/state.md` lists what stands between master and the `v1.0.0-rc.3` exit.
  Rows that are done leave the RC2 and RC3 dispositions: four close
  (the first full hosted run on master, the SYCL SpEED singular covariance on
  the Arc A380, the `ciede` math-library residual, which ADR-1426's measured
  bound closes, and a stale HIP `float_moment` gate row) and five that were
  already closed are no longer listed. Every remaining RC3 row states what is
  left, where it can be closed (this host, an Xe2 or Xe-LP device, or an Apple
  device) and whether the macOS tester bundle's report measures it.
