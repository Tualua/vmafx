- `AGENTS.md` section 10 (and the compiled vendor context files) and
  `docs/development/licence-provenance-check.md` now add the `upstream` remote
  with `git remote add --no-tags`, so a fresh clone does not fetch Netflix's
  tags back into `VMAFx/vmafx` after their removal (ADR-1805).
