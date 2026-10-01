- **Core and GitHub AGENTS.md files use the internal register.**
  `core/AGENTS.md`, `core/test/AGENTS.md`, `.github/AGENTS.md`,
  `core/src/AGENTS.md`, `core/tools/AGENTS.md`, `core/src/dnn/AGENTS.md`,
  `core/src/hip/AGENTS.md`, `core/src/cuda/AGENTS.md`, `scripts/AGENTS.md`,
  and `core/src/sycl/AGENTS.md` conform to the caveman register required by
  ADR-1249. Every code span, command, identifier, link, and invariant is
  preserved verbatim and verified against `praetorctl caveman check` and
  `caveman_keep_check.py`.
