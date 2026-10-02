- **`scripts/dev/relicense_fork_files.py --check` exits 0.** The tool no longer
  treats the exact-twin data fragments (`scripts/ci/exact_twins.d/`) or
  praetor's byte-locked files as sources that need a header, and it rewrites a
  licence grant only in a file's own header, so the mirrored-header template
  inside `scripts/sync-pelorus-interop.sh` is left alone. Five helper headers
  have `[ports]` entries in `scripts/dev/relicense_provenance.toml`, because
  their family names origins they do not reproduce; `sycl_ssimulacra2_math.h`
  resolves to libjxl instead of the SSIM lineages. Four headers that only
  configure and include a shared header, or hold a kernel argument block, have
  `[not_ports]` entries and stay `EUPL-1.2`
  (`T-RELICENSE-CHECK-PENDING-2026-10-02`,
  [ADR-1474](docs/adr/1474-relicense-helper-headers-and-ci-check.md)).
