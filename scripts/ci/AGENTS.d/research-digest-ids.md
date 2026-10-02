---
paths:
  - scripts/ci/*research-digest-id*
  - scripts/ci/tests/test_research_digest_ids.py
  - docs/research/*.md
invariant: New digest: unique numeric prefix + H1 `# Research-NNNN`; baseline debt only shrinks; no `--bootstrap-from-ref` in CI.
---
<!-- markdownlint-disable MD013 MD060 -->
# Research-digest identifier ratchet

`check-research-digest-ids.py` scans every `docs/research/NNNN-*.md`
digest except the `0000` template. New digests must have a unique numeric
prefix and a first H1 beginning `# Research-NNNN`, with the same number as
the filename. The generated `research-digest-id-baseline.json` records exact
pre-existing collision sets and exact non-canonical H1 text; those entries are
debt, not exceptions to copy. ADR-1335 binds every audit to the trusted merge
base: the branch baseline must exactly match its tree and may only reduce
trusted debt. Ordinary `--write` requires a canonical baseline at that merge
base. The separate `--bootstrap-from-ref` path accepts only a full immutable
ancestor that predates checker and baseline, refuses overwrite and debt growth,
and must never appear in CI or hook commands. Preserve the live trusted-ref
gate, deterministic writer, planted deletion/rewrite/growth red caps, both
local hooks, and Rule Enforcement invocation together.
