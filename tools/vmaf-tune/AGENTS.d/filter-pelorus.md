---
paths:
  - tools/vmaf-tune/src/vmaftune/filter_adapters/pelorus_deband.py
  - tools/vmaf-tune/tests/test_filter_adapter_pelorus_deband.py
invariant: 10 Pelorus deband knobs in pelorus_deband.py; dynamic ranges and defaults pinned by test suite.
---
<!-- markdownlint-disable MD024 -->
# Pelorus deband filter adapter

- **10 Pelorus deband knobs in `filter_adapters/pelorus_deband.py`
  are frozen two-repo contract, not free parameter
  ([ADR-1116](../../../docs/adr/1116-autotune-prefilter-control-plane.md);
  Pelorus ADR-0110).** `PELORUS_DEBAND_KNOBS` (name / type / `lo` /
  `hi` / `default`) mirrors Pelorus control-plane table verbatim. Do
  **not** widen, narrow, rename, retype, or reorder knob, and do
  **not** add `sample` / `blur` / `planes` / `meta` (deliberately
  out-of-contract). Change here only valid as half of coordinated
  Pelorus + vmafx PR pair. Conformance test
  `tests/test_filter_adapter_pelorus_deband.py` re-transcribes
  contract independently, fails on any drift — if it goes red after
  rebase, contract moved, not test. `filter_adapters/` family is
  sibling of `codec_adapters/`: *pre-filter* is not codec (no
  preset/CRF/two-pass surface), so two registries stay separate.
  `prefilter` joint TPE search (`prefilter.py`) builds search space
  straight from this table + synthetic `crf` axis, reuses `fast.py`
  `TPESampler` study — keep search-engine reuse rather than forking
  second sampler.
