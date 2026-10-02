- **`compat/python-vmaf/__init__.py` meets the HISS standard and carries its
  SPDX line.** `call_vmafexec()` (118 lines) and
  `call_vmafexec_multi_features()` (77 lines) are assembled from pure helpers
  that each emit one part of the command; the command text is unchanged and
  now pinned by two tests. HISS baseline: two rows fewer
  ([ADR-1142](docs/adr/1142-whole-codebase-standards.md),
  [ADR-1250](docs/adr/1250-eupl-fork-relicense.md)).
