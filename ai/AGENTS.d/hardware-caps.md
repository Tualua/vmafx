---
paths:
  - ai/data/hardware_caps.csv
  - ai/scripts/hardware_caps_loader.py
invariant: Hardware capability priors sourced from vendor docs; loader rejects benchmark, throughput, and quality columns.
---
<!-- markdownlint-disable MD013 MD060 -->
# Hardware capability priors

- **Hardware-capability priors are prior-only (ADR-0335).**
  [`ai/data/hardware_caps.csv`](../data/hardware_caps.csv) +
  [`ai/scripts/hardware_caps_loader.py`](../scripts/hardware_caps_loader.py)
  ship per-architecture GPU encode-block fingerprints (codecs
  supported, max resolution, encoding-block count, tensor /
  NPU flags, driver floor) sourced exclusively from primary
  vendor docs. Loader's schema rejects benchmark-shaped
  columns (`fps_*`, `throughput`, `mbps`, `latency`, `watts`,
  `tdp`, `score_*`, `vmaf_*`), community-wiki source URLs
  (`wikipedia.org`, `wikichip.org`), empty fields, and zero
  encoding-block rows. Adding throughput / quality numbers to this
  surface is forbidden by ADR-0335 and companion research digest's
  category-1 NO-GO finding. Performance signal must come from
  corpus's own measured rows, not from static prior table. Schema
  extensions (new capability columns) require new ADR, not silent
  column bump.
