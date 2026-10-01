- **Remaining core and scripts AGENTS.md files use the internal register.**
  `core/src/metal/AGENTS.md`, `core/src/mcp/AGENTS.md`,
  `core/include/libvmaf/AGENTS.md`, `scripts/lib/AGENTS.md`,
  `scripts/dev/AGENTS.md`, and `.zed/AGENTS.md` conform to the caveman register
  required by ADR-1249. Every code span, command, identifier, link, and
  invariant is preserved verbatim and verified against `praetorctl caveman check`
  and `caveman_keep_check.py`.
