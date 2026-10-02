---
paths:
  - ai/scripts/signal_mix_audit.py
  - docs/ai/signal-mix-audit.md
invariant: Signal-mix audit is advisory and side-effect free: no feature extraction, checkpoint export, or CI gating.
---
<!-- markdownlint-disable MD013 MD060 -->
# Signal-mix audit

- [ADR-0650](../../docs/adr/0650-signal-mix-audit.md) — **signal-mix audit is advisory.** `ai/scripts/signal_mix_audit.py` reads already-extracted parquet/JSONL tables, renders coverage, redundancy, complementary-intersection, and blind-spot reports. Must remain side-effect free: no feature extraction, no checkpoint export, no corpus mutation, no CI gating by default. Adding new metric families or table columns -> update family regexes and `docs/ai/signal-mix-audit.md` together so reports keep naming missing HDR/panel, saliency/ROI, texture, NR/MOS, and codec-profile signals in human terms.
