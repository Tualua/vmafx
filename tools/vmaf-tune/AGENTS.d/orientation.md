---
paths:
  - tools/vmaf-tune/pyproject.toml
  - tools/vmaf-tune/vmaf-tune
  - tools/vmaf-tune/src/vmaftune/__init__.py
invariant: Quality-aware encode automation harness; usage docs describe shipped status; multi-phase architecture.
---
<!-- markdownlint-disable MD024 -->
# Orientation: scope, phases, and workflows

Quality-aware encode automation harness. See
[`docs/adr/0237-quality-aware-encode-automation.md`](../../../docs/adr/0237-quality-aware-encode-automation.md)
for umbrella spec and
[`docs/research/0044-quality-aware-encode-automation.md`](../../../docs/research/0044-quality-aware-encode-automation.md)
for option-space digest.

- **Usage docs describe shipped implementation status.**
  Dedicated `docs/usage/vmaf-tune-*.md` pages and umbrella
  `docs/usage/vmaf-tune.md` page are user-discoverable contracts,
  not backlog scratch space. When tune surface leaves scaffold
  state, update both standalone page and umbrella page in same PR;
  do not leave `(stub)`, `scaffold-only`, or stale CLI names on
  paths backed by implementation and tests.

Phases B (target-VMAF bisect), C (per-title CRF predictor), E
(Pareto ABR ladder) and F (MCP tools) per ADR-0237 are explicitly
out of scope here; do not add bisect / predictor / ladder / MCP
code into this tree without ADR-0237 follow-up promoting
corresponding phase.
