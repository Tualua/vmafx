---
paths:
  - ai/scripts/materialize_second_opinion_features.py
  - ai/scripts/batch_materialize_second_opinion_features.py
invariant: Second-opinion materialization stays table-side without external scorer invocations; namespaced columns.
---
<!-- markdownlint-disable MD013 MD060 -->
# Second-opinion feature materialization

- [ADR-0657](../../docs/adr/0657-second-opinion-feature-materializer.md) — **second-opinion materialisation stays table-side.** `ai/scripts/materialize_second_opinion_features.py` joins already-generated NR/MOS scorer JSON onto feature tables; must not invoke, vendor, or link third-party VQA competitors. Keep output columns namespaced as `second_opinion_<scorer>_*`, preserve row key by default, treat duplicate scorer/key rows as data poisoning rather than averaging them silently.
- [ADR-0674](../../docs/adr/0674-second-opinion-materializer-batch-manifest.md) — **second-opinion batch manifests only orchestrate joins.** `ai/scripts/batch_materialize_second_opinion_features.py` may select multiple feature tables and score sidecars. Must call shared table-side materializer; must not invoke external scorer binaries or duplicate key/status/column logic.
